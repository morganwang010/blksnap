#ifndef SNAP_LIB_SNAPSHOT_IOCTL_H
#define SNAP_LIB_SNAPSHOT_IOCTL_H

#ifdef  __cplusplus
extern "C" {
#endif

#define SUCCESS 0

#define MAX_TRACKING_DEVICE_COUNT	256

#define VEEAM_SNAP	0x56730000  // 'sV' <-> "Vs"

#pragma pack(push,1)
//////////////////////////////////////////////////////////////////////////
// version

#define VEEAMSNAP_COMPATIBILITY_SNAPSTORE 0x0000000000000001ull
//#define VEEAMSNAP_COMPATIBILITY_BTRFS     0x0000000000000002ull
#define VEEAMSNAP_COMPATIBILITY_MULTIDEV  0x0000000000000004ull

struct ioctl_compatibility_flags_s {
	unsigned long long flags;
};
#define IOCTL_COMPATIBILITY_FLAGS	_IOW(VEEAM_SNAP, 0, struct ioctl_compatibility_flags_s)

struct ioctl_getversion_s {
	unsigned short major;
	unsigned short minor;
	unsigned short revision;
	unsigned short build;
};
#define IOCTL_GETVERSION	_IOW(VEEAM_SNAP, 1, struct ioctl_getversion_s)

//////////////////////////////////////////////////////////////////////////
// tracking
#define IOCTL_TRACKING_ADD		_IOW(VEEAM_SNAP, 2, struct ioctl_dev_id_s)


#define IOCTL_TRACKING_REMOVE	_IOW(VEEAM_SNAP, 3, struct ioctl_dev_id_s)

#define IOCTL_TRACKING_COLLECT		_IOW(VEEAM_SNAP, 4, struct ioctl_tracking_collect_s)


#define IOCTL_TRACKING_BLOCK_SIZE	_IOW(VEEAM_SNAP, 5, unsigned int)


struct ioctl_tracking_read_cbt_bitmap_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int offset;
	unsigned int length;
	union{
		unsigned char* buff;
		unsigned long long ull_buff;
	};
};
#define IOCTL_TRACKING_READ_CBT_BITMAP		_IOR(VEEAM_SNAP, 6, struct ioctl_tracking_read_cbt_bitmap_s)

struct block_range_s
{
    unsigned long long ofs;//sectors
    unsigned long long cnt;//sectors
};

struct ioctl_tracking_mark_dirty_blocks_s{
    struct ioctl_dev_id_s image_dev_id;
    unsigned int count;
    union{
        struct block_range_s* p_dirty_blocks;
        unsigned long long ull_dirty_blocks;
    };
};
#define IOCTL_TRACKING_MARK_DIRTY_BLOCKS _IOR(VEEAM_SNAP, 7, struct ioctl_tracking_mark_dirty_blocks_s)
//////////////////////////////////////////////////////////////////////////
// snapshot

struct ioctl_snapshot_create_s{
	unsigned long long snapshot_id;
	unsigned int count;
	union{
		struct ioctl_dev_id_s* p_dev_id;
		unsigned long long ull_dev_id;
	};
};
#define IOCTL_SNAPSHOT_CREATE		_IOW(VEEAM_SNAP, 0x10, struct ioctl_snapshot_create_s)


#define IOCTL_SNAPSHOT_DESTROY		_IOR(VEEAM_SNAP, 0x11, unsigned long long )


struct ioctl_snapshot_errno_s{
	struct ioctl_dev_id_s dev_id;
	int err_code;
};
#define IOCTL_SNAPSHOT_ERRNO    _IOW(VEEAM_SNAP, 0x12, struct ioctl_snapshot_errno_s)


struct ioctl_range_s{
	unsigned long long left;
	unsigned long long right;
};

//////////////////////////////////////////////////////////////////////////
// snapstore
struct ioctl_snapstore_create_s
{
	unsigned char id[16];
	struct ioctl_dev_id_s snapstore_dev_id;
	unsigned int count;
	union{
		struct ioctl_dev_id_s* p_dev_id;
		unsigned long long ull_dev_id;
	};
};
#define IOCTL_SNAPSTORE_CREATE _IOR(VEEAM_SNAP, 0x28, struct ioctl_snapstore_create_s)


struct ioctl_snapstore_file_add_s
{
	unsigned char id[16];
	unsigned int range_count;
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_SNAPSTORE_FILE _IOR(VEEAM_SNAP, 0x29, struct ioctl_snapstore_file_add_s)


struct ioctl_snapstore_memory_limit_s
{
	unsigned char id[16];
	unsigned long long size;
};
#define IOCTL_SNAPSTORE_MEMORY _IOR(VEEAM_SNAP, 0x2A, struct ioctl_snapstore_memory_limit_s)


struct ioctl_snapstore_cleanup_s
{
	unsigned char id[16];
	unsigned long long filled_bytes;
};
#define IOCTL_SNAPSTORE_CLEANUP _IOW(VEEAM_SNAP, 0x2B, struct ioctl_snapstore_cleanup_s)

struct ioctl_snapstore_file_add_multidev_s
{
	unsigned char id[16];
	struct ioctl_dev_id_s dev_id;
	unsigned int range_count;
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_SNAPSTORE_FILE_MULTIDEV _IOR(VEEAM_SNAP, 0x2C, struct ioctl_snapstore_file_add_multidev_s)
//////////////////////////////////////////////////////////////////////////
// collect snapshot data location

struct image_info_s{
	struct ioctl_dev_id_s original_dev_id;
	struct ioctl_dev_id_s snapshot_dev_id;
};

struct ioctl_collect_shapshot_images_s{
	int count;     //
	union{
		struct image_info_s* p_image_info;
		unsigned long long ull_image_info;
	};
};
#define IOCTL_COLLECT_SNAPSHOT_IMAGES _IOW(VEEAM_SNAP, 0x30, struct ioctl_collect_shapshot_images_s)


struct ioctl_collect_snapshotdata_location_start_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int magic_length;
	union{
		void* magic_buff;
		unsigned long long ull_buff;
	};
};
#define  IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_START _IOW(VEEAM_SNAP, 0x40, struct ioctl_collect_snapshotdata_location_start_s )


struct ioctl_collect_snapshotdata_location_get_s{
	struct ioctl_dev_id_s dev_id;
	unsigned int range_count;
	union{
		struct ioctl_range_s* ranges;
		unsigned long long ull_ranges;
	};
};
#define IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_GET		_IOW(VEEAM_SNAP, 0x41, struct ioctl_collect_snapshotdata_location_get_s)


struct ioctl_collect_snapshotdata_location_complete_s{
	struct ioctl_dev_id_s dev_id;
};
#define  IOCTL_COLLECT_SNAPSHOTDATA_LOCATION_COMPLETE _IOR(VEEAM_SNAP, 0x42, struct ioctl_collect_snapshotdata_location_complete_s )

//////////////////////////////////////////////////////////////////////////
// persistent CBT data parameter

struct ioctl_persistentcbt_data_s
{
    unsigned int size;
    const char* parameter;
};
#define  IOCTL_PERSISTENTCBT_DATA _IOR(VEEAM_SNAP, 0x48, struct ioctl_persistentcbt_data_s )

//////////////////////////////////////////////////////////////////////////
// debug and support
#define IOCTL_PRINTSTATE _IO(VEEAM_SNAP, 0x80)

#ifdef SNAPIMAGE_TRACER
#define VEEAM_IMAGE   0x69730000  // 'iV' <-> "Vi"

//////////////////////////////////////////////////////////////////////////
// io control for snapshot image 
typedef struct trace_record_s
{
    unsigned long long time;
    unsigned long long sector_ofs;
    unsigned int size;
    int direction;
}trace_record_t;

struct ioctl_image_trace_read_s{
    unsigned int capacity;
    unsigned int count;
    trace_record_t* records;
};

#define IOCTL_IMAGE_TRACE_READ _IOW(VEEAM_IMAGE, 0x81, struct ioctl_image_trace_read_s)

#endif
#pragma pack(pop)

// commands for character device interface
#define VEEAMSNAP_CHARCMD_UNDEFINED 0x00
#define VEEAMSNAP_CHARCMD_ACKNOWLEDGE 0x01
#define VEEAMSNAP_CHARCMD_INVALID 0xFF
// to module commands
#define VEEAMSNAP_CHARCMD_INITIATE 0x21
#define VEEAMSNAP_CHARCMD_NEXT_PORTION 0x22
#define VEEAMSNAP_CHARCMD_NEXT_PORTION_MULTIDEV 0x23
// from module commands
#define VEEAMSNAP_CHARCMD_HALFFILL 0x41
#define VEEAMSNAP_CHARCMD_OVERFLOW 0x42
#define VEEAMSNAP_CHARCMD_TERMINATE 0x43


#endif // SNAP_LIB_SNAPSHOT_IOCTL_H