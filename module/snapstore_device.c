#include "common.h"
#include "snapstore_device.h"
#include "snapstore.h"
#include "snapstore_blk.h"
#include "blk_util.h"

#define SECTION "snapstore "
#include "log_format.h"

int inc_snapstore_block_size_pow(void);

LIST_HEAD(snapstore_devices);
DECLARE_RWSEM(snapstore_devices_lock);

static inline void _snapstore_device_descr_write_lock( snapstore_device_t* snapstore_device )
{
	mutex_lock( &snapstore_device->store_block_map_locker );
}
static inline void _snapstore_device_descr_write_unlock( snapstore_device_t* snapstore_device )
{
	mutex_unlock( &snapstore_device->store_block_map_locker );
}

void snapstore_device_done( void )
{
	snapstore_device_t* snapstore_device = NULL;

	do {
		down_write(&snapstore_devices_lock);
		if (!list_empty(&snapstore_devices)) {
			snapstore_device = list_entry( snapstore_devices.next, snapstore_device_t, link );
			list_del( &snapstore_device->link );
		}
		up_write(&snapstore_devices_lock);

		if (snapstore_device)
			snapstore_device_put_resource( snapstore_device );
	} while(snapstore_device);
}

snapstore_device_t* snapstore_device_find_by_dev_id( dev_t dev_id )
{
	snapstore_device_t* result = NULL;

	down_read( &snapstore_devices_lock );
	if (!list_empty( &snapstore_devices )) {
		struct list_head* _head;

		list_for_each( _head, &snapstore_devices ) {
			snapstore_device_t* snapstore_device = list_entry( _head, snapstore_device_t, link );

			if (dev_id == snapstore_device->dev_id){
				result = snapstore_device;
				break;
			}
		}
	}
	up_read( &snapstore_devices_lock );

	return result;
}

snapstore_device_t* _snapstore_device_get_by_snapstore_id( uuid_t* id )
{
	snapstore_device_t* result = NULL;

	down_write( &snapstore_devices_lock );
	if (!list_empty( &snapstore_devices )) {
		struct list_head* _head;

		list_for_each( _head, &snapstore_devices ) {
			snapstore_device_t* snapstore_device = list_entry( _head, snapstore_device_t, link );

			if (uuid_equal( id, &snapstore_device->snapstore->id )) {

				result = snapstore_device;
				list_del( &snapstore_device->link );
				break;
			}
		}
	}
	up_write( &snapstore_devices_lock );

	return result;
}

void _snapstore_device_destroy( snapstore_device_t* snapstore_device )
{
	log_tr("Destroy snapstore device");

	xa_destroy(&snapstore_device->store_block_map);

	if (snapstore_device->orig_blk_dev != NULL)
		blk_dev_close( snapstore_device->orig_blk_dev );

	rangevector_done( &snapstore_device->zero_sectors );

	if (snapstore_device->snapstore){
		log_tr_uuid("Snapstore uuid ", (&snapstore_device->snapstore->id));

		snapstore_put( snapstore_device->snapstore );
		snapstore_device->snapstore = NULL;
	}

	kfree( snapstore_device );
}

void snapstore_device_free_cb( void* resource )
{
	snapstore_device_t* snapstore_device = (snapstore_device_t*)resource;

	_snapstore_device_destroy( snapstore_device );
}

int snapstore_device_cleanup( uuid_t* id )
{
	int result = SUCCESS;
	snapstore_device_t* snapstore_device = NULL;

	while (NULL != (snapstore_device = _snapstore_device_get_by_snapstore_id( id ))){
		log_tr_dev_t( "Cleanup snapstore device for device ", snapstore_device->dev_id );

		snapstore_device_put_resource( snapstore_device );
	}
	return result;
}

int snapstore_device_create( dev_t dev_id, snapstore_t* snapstore )
{
	int res = SUCCESS;
	snapstore_device_t* snapstore_device = kzalloc(sizeof(snapstore_device_t), GFP_KERNEL);

	if (NULL == snapstore_device)
		return -ENOMEM;

	INIT_LIST_HEAD( &snapstore_device->link );
	snapstore_device->dev_id = dev_id;

	res = blk_dev_open( dev_id, &snapstore_device->orig_blk_dev );
	if (res != SUCCESS) {
		kfree(snapstore_device);

		log_err_dev_t( "Unable to create snapstore device: failed to open original device ", dev_id );
		return res;
	}

	shared_resource_init( &snapstore_device->shared, snapstore_device, snapstore_device_free_cb );

	snapstore_device->snapstore = NULL;
	snapstore_device->err_code = SUCCESS;
	snapstore_device->corrupted = false;
	atomic_set( &snapstore_device->req_failed_cnt, 0 );

	mutex_init(&snapstore_device->store_block_map_locker);

	rangevector_init(&snapstore_device->zero_sectors);

	xa_init(&snapstore_device->store_block_map);

	snapstore_device->snapstore = snapstore_get(snapstore);

	down_write( &snapstore_devices_lock );
	list_add_tail( &snapstore_device->link, &snapstore_devices );
	up_write( &snapstore_devices_lock );

	snapstore_device_get_resource(snapstore_device);

	return SUCCESS;
}

int snapstore_device_add_request( snapstore_device_t* snapstore_device, unsigned long block_index, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	blk_descr_unify_t* blk_descr = NULL;
	blk_deferred_t* dio = NULL;
	bool req_new = false;

	blk_descr = snapstore_get_empty_block( snapstore_device->snapstore );
	if (blk_descr == NULL){
		log_err( "Unable to add block to defer IO request: failed to allocate next block" );
		return -ENODATA;
	}

	res = xa_err(xa_store( &snapstore_device->store_block_map, block_index, blk_descr, GFP_NOIO ));
	if (res != SUCCESS){
		log_err_d( "Unable to add block to defer IO request: failed to set block descriptor to descriptors array. errno=", res );
		return res;
	}

	if (*dio_copy_req == NULL){
		*dio_copy_req = blk_deferred_request_new( );
		if (*dio_copy_req == NULL){
			log_err( "Unable to add block to defer IO request: failed to allocate defer IO request" );
			return -ENOMEM;

		}
		req_new = true;
	}

	do{
		dio = blk_deferred_alloc( block_index, blk_descr );
		if (dio == NULL){
			log_err( "Unabled to add block to defer IO request: failed to allocate defer IO" );
			res = -ENOMEM;
			break;
		}

		res = blk_deferred_request_add( *dio_copy_req, dio );
		if (res != SUCCESS){
			log_err( "Unable to add block to defer IO request: failed to add defer IO to request" );
		}
	} while (false);

	if (res != SUCCESS){
		if (dio != NULL){
			blk_deferred_free( dio );
			dio = NULL;
		}
		if (req_new){
			blk_deferred_request_free( *dio_copy_req );
			*dio_copy_req = NULL;
		}
	}

	return res;
}

int snapstore_device_prepare_requests( snapstore_device_t* snapstore_device, struct blk_range* copy_range, blk_deferred_request_t** dio_copy_req )
{
	int res = SUCCESS;
	unsigned long inx = 0;
	unsigned long first = (unsigned long)(copy_range->ofs >> snapstore_block_shift());
	unsigned long last = (unsigned long)((copy_range->ofs + copy_range->cnt - 1) >> snapstore_block_shift());

	for (inx = first; inx <= last; inx++){
		if (NULL != xa_load(&snapstore_device->store_block_map, inx)) {
			//log_tr_sz( "Already stored block # ", inx );
		} else {
			res = snapstore_device_add_request( snapstore_device, inx, dio_copy_req );
			if ( res != SUCCESS){
				log_err_d( "Failed to create copy defer IO request. errno=", res );
				break;
			}
		}
	}
	if (res != SUCCESS){
		snapstore_device_set_corrupted( snapstore_device, res );
	}

	return res;
}

int snapstore_device_store( snapstore_device_t* snapstore_device, blk_deferred_request_t* dio_copy_req )
{
	int res = snapstore_request_store( snapstore_device->snapstore, dio_copy_req );
	if (res != SUCCESS)
		snapstore_device_set_corrupted( snapstore_device, res );

	return res;
}

int snapstore_device_read( snapstore_device_t* snapstore_device, blk_redirect_bio_t* rq_redir )
{
	int res = SUCCESS;

	unsigned long block_index;
	unsigned long block_index_last;
	unsigned long block_index_first;

	sector_t blk_ofs_start = 0;		 //device range start
	sector_t blk_ofs_count = 0;		 //device range length

	struct blk_range rq_range;
	rangevector_t* zero_sectors = &snapstore_device->zero_sectors;

	if (snapstore_device_is_corrupted( snapstore_device ))
		return -ENODATA;

	rq_range.cnt = bio_sectors(rq_redir->bio);
	rq_range.ofs = rq_redir->bio->bi_iter.bi_sector;

	if (!bio_has_data( rq_redir->bio )){
		log_warn_sz( "Empty bio was found during reading from snapstore device. flags=", rq_redir->bio->bi_flags );

		blk_redirect_complete( rq_redir, SUCCESS );
		return SUCCESS;
	}

	block_index_first = (unsigned long)(rq_range.ofs >> snapstore_block_shift());
	block_index_last = (unsigned long)((rq_range.ofs + rq_range.cnt - 1) >> snapstore_block_shift());

	_snapstore_device_descr_write_lock( snapstore_device );
	for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
		blk_descr_unify_t* blk_descr = NULL;

		blk_ofs_count = min_t( sector_t,
			(((sector_t)(block_index + 1)) << snapstore_block_shift()) - (rq_range.ofs + blk_ofs_start),
			rq_range.cnt - blk_ofs_start );

		blk_descr = (blk_descr_unify_t*)xa_load(&snapstore_device->store_block_map, block_index);
		if (blk_descr) {
			//push snapstore read
			res = snapstore_redirect_read( rq_redir, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
			if (res != SUCCESS){
				log_err( "Failed to read from snapstore device" );
				break;
			}
		} else {

			//device read with zeroing
			if (zero_sectors)
				res = blk_dev_redirect_read_zeroed( rq_redir, snapstore_device->orig_blk_dev, rq_range.ofs, blk_ofs_start, blk_ofs_count, zero_sectors );
			else
				res = blk_dev_redirect_part( rq_redir, READ, snapstore_device->orig_blk_dev, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );

			if (res != SUCCESS){
				log_err_dev_t( "Failed to redirect read request to the original device ", snapstore_device->dev_id );
				break;
			}
		}

		blk_ofs_start += blk_ofs_count;
	}

	if (res == SUCCESS){
		if (atomic64_read( &rq_redir->bio_count ) > 0ll) //async direct access needed
			blk_dev_redirect_submit( rq_redir );
		else
			blk_redirect_complete( rq_redir, res );
	}
	else{
		log_err_d( "Failed to read from snapstore device. errno=", res );
		log_err_format( "Position %lld sector, length %lld sectors", rq_range.ofs, rq_range.cnt );
	}
	_snapstore_device_descr_write_unlock(snapstore_device);

	return res;
}

int _snapstore_device_copy_on_write( snapstore_device_t* snapstore_device, struct blk_range* rq_range )
{
	int res = SUCCESS;
	blk_deferred_request_t* dio_copy_req = NULL;

	_snapstore_device_descr_read_lock( snapstore_device );
	do{
		res = snapstore_device_prepare_requests( snapstore_device, rq_range, &dio_copy_req );
		if (res != SUCCESS){
			log_err_d( "Failed to create defer IO request for range. errno=", res );
			break;
		}

		if (NULL == dio_copy_req)
			break;//nothing to copy

		res = blk_deferred_request_read_original( snapstore_device->orig_blk_dev, dio_copy_req );
		if (res != SUCCESS){
			log_err_d( "Failed to read data from the original device. errno=", res );
			break;
		}
		res = snapstore_device_store( snapstore_device, dio_copy_req );
		if (res != SUCCESS){
			log_err_d( "Failed to write data to snapstore. errno=", res );
			break;
		}
	} while (false);
	_snapstore_device_descr_read_unlock( snapstore_device );

	if (dio_copy_req){
		if (res == -EDEADLK)
			blk_deferred_request_deadlocked( dio_copy_req );
		else
			blk_deferred_request_free( dio_copy_req );
	}

	return res;
}


int snapstore_device_write( snapstore_device_t* snapstore_device, blk_redirect_bio_t* rq_redir )
{
	int res = SUCCESS;

	unsigned long block_index;
	unsigned long block_index_last;
	unsigned long block_index_first;

	sector_t blk_ofs_start = 0;		 //device range start
	sector_t blk_ofs_count = 0;		 //device range length

	struct blk_range rq_range;

	BUG_ON( NULL == snapstore_device );
	BUG_ON( NULL == rq_redir );
	BUG_ON( NULL == rq_redir->bio );

	if (snapstore_device_is_corrupted( snapstore_device ))
		return -ENODATA;

	rq_range.cnt = bio_sectors(rq_redir->bio);
	rq_range.ofs = rq_redir->bio->bi_iter.bi_sector;

	if (!bio_has_data( rq_redir->bio )){
		log_warn_sz( "Empty bio was found during reading from snapstore device. flags=", rq_redir->bio->bi_flags );

		blk_redirect_complete( rq_redir, SUCCESS );
		return SUCCESS;
	}

	// do copy to snapstore previously
	res = _snapstore_device_copy_on_write( snapstore_device, &rq_range );

	block_index_first = (unsigned long)(rq_range.ofs >> snapstore_block_shift());
	block_index_last = (unsigned long)((rq_range.ofs + rq_range.cnt - 1) >> snapstore_block_shift());

	_snapstore_device_descr_write_lock(snapstore_device);
	for (block_index = block_index_first; block_index <= block_index_last; ++block_index){
		blk_descr_unify_t* blk_descr = NULL;

		blk_ofs_count = min_t( sector_t,
			(((sector_t)(block_index + 1)) << snapstore_block_shift()) - (rq_range.ofs + blk_ofs_start),
			rq_range.cnt - blk_ofs_start );

		blk_descr = (blk_descr_unify_t*)xa_load(&snapstore_device->store_block_map, block_index);
		if (blk_descr == NULL){
			log_err( "Unable to write from snapstore device: invalid snapstore block descriptor" );
			res = -EIO;
			break;
		}

		res = snapstore_redirect_write( rq_redir, snapstore_device->snapstore, blk_descr, rq_range.ofs + blk_ofs_start, blk_ofs_start, blk_ofs_count );
		if (res != SUCCESS){
			log_err( "Unable to write from snapstore device: failed to redirect write request to snapstore" );
			break;
		}

		blk_ofs_start += blk_ofs_count;
	}
	if (res == SUCCESS){
		if (atomic64_read( &rq_redir->bio_count ) > 0){ //async direct access needed
			blk_dev_redirect_submit( rq_redir );
		}
		else{
			blk_redirect_complete( rq_redir, res );
		}
	}
	else{
		log_err_d( "Failed to write from snapstore device. errno=", res );
		log_err_format( "Position %lld sector, length %lld sectors", rq_range.ofs, rq_range.cnt );

		snapstore_device_set_corrupted( snapstore_device, res );
	}
	_snapstore_device_descr_write_unlock(snapstore_device);
	return res;
}

bool snapstore_device_is_corrupted( snapstore_device_t* snapstore_device )
{
	if (snapstore_device == NULL)
		return true;

	if (snapstore_device->corrupted){
		if (0 == atomic_read( &snapstore_device->req_failed_cnt )){
			log_err_dev_t( "Snapshot device is corrupted for ", snapstore_device->dev_id );
		}
		atomic_inc( &snapstore_device->req_failed_cnt );
		return true;
	}

	return false;
}

void snapstore_device_set_corrupted( snapstore_device_t* snapstore_device, int err_code )
{
	if (!snapstore_device->corrupted) {
		atomic_set( &snapstore_device->req_failed_cnt, 0 );
		snapstore_device->corrupted = true;
		snapstore_device->err_code = abs(err_code);

		log_err_dev_t( "Set snapshot device is corrupted for ", snapstore_device->dev_id );
	}
}

void snapstore_device_print_state( snapstore_device_t* snapstore_device )
{
	log_tr( "" );
	log_tr_dev_t( "Snapstore device state for device ", snapstore_device->dev_id );

	if (snapstore_device->corrupted){
		log_tr( "Corrupted");
		log_tr_d( "Failed request count: ", atomic_read( &snapstore_device->req_failed_cnt ) );
	}
}

int snapstore_device_errno( dev_t dev_id, int* p_err_code )
{
	snapstore_device_t* snapstore_device = snapstore_device_find_by_dev_id( dev_id );
	if (snapstore_device == NULL)
		return -ENODATA;

	*p_err_code = snapstore_device->err_code;
	return SUCCESS;
}
