/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_SNAPIMAGE_H
#define __BLK_SNAP_SNAPIMAGE_H

#ifdef HAVE_GENHD_H
#include <linux/genhd.h>
#endif
#include <linux/blk-mq.h>
#include <linux/kthread.h>

struct diff_area;
struct cbt_map;

/**
 * struct snapimage - Snapshot image block device.
 *
 * @capacity:
 *	The size of the snapshot image in sectors must be equal to the size
 *	of the original device at the time of taking the snapshot.
 * @worker:
 *	A pointer to the &struct task of the worker thread that process I/O
 *      units.
 * @queue_lock:
 *      Lock for &queue.
 * @queue:
 *	A queue of I/O units waiting to be processed.
 * @disk:
 *	A pointer to the &struct gendisk for the image block device.
 * @diff_area:
 *	A pointer to the owned &struct diff_area.
 * @cbt_map:
 *	A pointer to the owned &struct cbt_map.
 *
 * The snapshot image is presented in the system as a block device. But
 * when reading or writing a snapshot image, the data is redirected to
 * the original block device or to the block device of the difference storage.
 *
 * The module does not prohibit reading and writing data to the snapshot
 * from different threads in parallel. To avoid the problem with simultaneous
 * access, it is enough to open the snapshot image block device with the
 * FMODE_EXCL parameter.
 */
struct snapimage {
	sector_t capacity;

	struct task_struct *worker;
	spinlock_t queue_lock;
	struct bio_list queue;

	struct gendisk *disk;

	struct diff_area *diff_area;
	struct cbt_map *cbt_map;
};

void snapimage_free(struct snapimage *snapimage);
struct snapimage *snapimage_create(struct diff_area *diff_area,
				   struct cbt_map *cbt_map);

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
int snapimage_get_chunk_state(struct snapimage *snapimage, sector_t sector,
			      struct blk_snap_sector_state *state);
#endif
#endif /* __BLK_SNAP_SNAPIMAGE_H */
