/*
 * Copyright (c) 2012-2016, 2019-2020 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

#include <adf_nbuf.h>         /* adf_nbuf_t, etc. */
#include <htt.h>              /* HTT_TX_EXT_TID_MGMT */
#include <ol_htt_tx_api.h>    /* htt_tx_desc_tid */
#include <ol_txrx_api.h>      /* ol_txrx_vdev_handle */
#include <ol_txrx_ctrl_api.h> /* ol_txrx_sync */
#include <ol_txrx_internal.h> /* TXRX_ASSERT1 */
#include <ol_txrx_types.h>    /* pdev stats, etc. */
#include <ol_tx_desc.h>       /* ol_tx_desc */
#include <ol_tx_send.h>       /* ol_tx_send */
#include <ol_tx_sched.h>      /* OL_TX_SCHED, etc. */
#include <ol_tx_queue.h>
#include <ol_txrx.h>
#include <adf_os_types.h>     /* a_bool_t */

#if defined(CONFIG_HL_SUPPORT)

#if defined(DEBUG_HL_LOGGING)
static void
ol_tx_sched_log(struct ol_txrx_pdev_t *pdev);
#define OL_TX_SCHED_LOG ol_tx_sched_log
#else
#define OL_TX_SCHED_LOG(pdev) /* no-op */
#endif /* defined(DEBUG_HL_LOGGING) */

#if DEBUG_HTT_CREDIT
#define OL_TX_DISPATCH_LOG_CREDIT()                                           \
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO_LOW,               \
        "                                TX %d bytes\n", adf_nbuf_len(msdu)); \
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO_LOW,               \
        " <HTT> Decrease credit %d - 1 = %d, len:%d.\n",                      \
            adf_os_atomic_read(&pdev->target_tx_credit),                      \
            adf_os_atomic_read(&pdev->target_tx_credit) -1,                   \
            adf_nbuf_len(msdu));
#else
#define OL_TX_DISPATCH_LOG_CREDIT()
#endif

/*--- generic definitions used by the scheduler framework for all algs ---*/

struct ol_tx_sched_ctx {
    ol_tx_desc_list head;
    int frms;
};

typedef TAILQ_HEAD(ol_tx_frms_queue_list_s, ol_tx_frms_queue_t)
    ol_tx_frms_queue_list;

#define OL_A_MAX(_x, _y) ((_x) > (_y) ? (_x) : (_y))

#define OL_A_MIN(_x, _y) ((_x) < (_y) ? (_x) : (_y))

/*--- scheduler algorithm selection ---*/

/*--- scheduler options ---------------------------------------------------
 * 1. Round-robin scheduler:
 *    Select the TID that is at the head of the list of active TIDs.
 *    Select the head tx queue for this TID.
 *    Move the tx queue to the back of the list of tx queues for this TID.
 *    Move the TID to the back of the list of active TIDs.
 *    Send as many frames from the tx queue as credit allows.
 * 2. Weighted-round-robin advanced scheduler:
 *    Keep an ordered list of which TID gets selected next.
 *    Use a weighted-round-robin scheme to determine when to promote a TID
 *    within this list.
 *    If a TID at the head of the list is inactive, leave it at the head,
 *    but check the next TIDs.
 *    If the credit available is less than the credit threshold for the
 *    next active TID, don't send anything, and leave the TID at the head
 *    of the list.
 *    After a TID is selected, move it to the back of the list.
 *    Select the head tx queue for this TID.
 *    Move the tx queue to the back of the list of tx queues for this TID.
 *    Send no more frames than the limit specified for the TID.
 */
#define OL_TX_SCHED_RR  1
#define OL_TX_SCHED_WRR_ADV 2

#ifndef OL_TX_SCHED
//#define OL_TX_SCHED OL_TX_SCHED_RR
#define OL_TX_SCHED OL_TX_SCHED_WRR_ADV /* default */
#endif


#if OL_TX_SCHED == OL_TX_SCHED_RR

#define ol_tx_sched_rr_t ol_tx_sched_t

#define OL_TX_SCHED_NUM_CATEGORIES (OL_TX_NUM_TIDS + OL_TX_VDEV_NUM_QUEUES)

#define ol_tx_sched_init                ol_tx_sched_init_rr
#define ol_tx_sched_select_init(pdev)   /* no-op */
#define ol_tx_sched_select_batch        ol_tx_sched_select_batch_rr
#define ol_tx_sched_txq_enqueue         ol_tx_sched_txq_enqueue_rr
#define ol_tx_sched_txq_deactivate      ol_tx_sched_txq_deactivate_rr
#define ol_tx_sched_category_tx_queues  ol_tx_sched_category_tx_queues_rr
#define ol_tx_sched_txq_discard         ol_tx_sched_txq_discard_rr
#define ol_tx_sched_category_info       ol_tx_sched_category_info_rr
#define ol_tx_sched_discard_select_category \
    ol_tx_sched_discard_select_category_rr

#elif OL_TX_SCHED == OL_TX_SCHED_WRR_ADV

#define ol_tx_sched_wrr_adv_t ol_tx_sched_t

#define OL_TX_SCHED_NUM_CATEGORIES OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES

#define ol_tx_sched_init                ol_tx_sched_init_wrr_adv
#define ol_tx_sched_select_init(pdev) \
    do { \
        adf_os_spin_lock_bh(&pdev->tx_queue_spinlock); \
        ol_tx_sched_select_init_wrr_adv(pdev); \
        adf_os_spin_unlock_bh(&pdev->tx_queue_spinlock); \
    } while (0)
#define ol_tx_sched_select_batch        ol_tx_sched_select_batch_wrr_adv
#define ol_tx_sched_txq_enqueue         ol_tx_sched_txq_enqueue_wrr_adv
#define ol_tx_sched_txq_deactivate      ol_tx_sched_txq_deactivate_wrr_adv
#define ol_tx_sched_category_tx_queues  ol_tx_sched_category_tx_queues_wrr_adv
#define ol_tx_sched_txq_discard         ol_tx_sched_txq_discard_wrr_adv
#define ol_tx_sched_category_info       ol_tx_sched_category_info_wrr_adv
#define ol_tx_sched_discard_select_category \
    ol_tx_sched_discard_select_category_wrr_adv

#else

#error Unknown OL TX SCHED specification

#endif /* OL_TX_SCHED */

/*--- round-robin scheduler -------------------------------------------------*/
#if OL_TX_SCHED == OL_TX_SCHED_RR

/*--- definitions ---*/

struct ol_tx_active_queues_in_tid_t {
    /* list_elem is used to queue up into up level queues*/
    TAILQ_ENTRY(ol_tx_active_queues_in_tid_t) list_elem;
    u_int32_t frms;
    u_int32_t bytes;
    ol_tx_frms_queue_list head;
    bool    active;
    int tid;
};

typedef TAILQ_HEAD(ol_tx_active_tids_s, ol_tx_active_queues_in_tid_t)
    ol_tx_active_tids_list;

struct ol_tx_sched_rr_t {
    struct ol_tx_active_queues_in_tid_t
        tx_active_queues_in_tid_array[OL_TX_NUM_TIDS + OL_TX_VDEV_NUM_QUEUES];
    ol_tx_active_tids_list tx_active_tids_list;
    u_int8_t discard_weights[OL_TX_NUM_TIDS + OL_TX_VDEV_NUM_QUEUES];
};

#define TX_SCH_MAX_CREDIT_FOR_THIS_TID(tidq) 16

/*--- functions ---*/

/*
 * The scheduler sync spinlock has been acquired outside this function,
 * so there is no need to worry about mutex within this function.
 */
static int
ol_tx_sched_select_batch_rr(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_sched_ctx *sctx,
    u_int32_t credit)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;
    struct ol_tx_frms_queue_t *next_tq;
    u_int16_t frames, used_credits = 0, tx_limit, tx_limit_flag = 0;
    int bytes;

    TX_SCHED_DEBUG_PRINT("Enter %s\n", __func__);

    if (TAILQ_EMPTY(&scheduler->tx_active_tids_list))
	return used_credits;

    txq_queue = TAILQ_FIRST(&scheduler->tx_active_tids_list);

    TAILQ_REMOVE(&scheduler->tx_active_tids_list, txq_queue, list_elem);
    txq_queue->active = FALSE;

    next_tq = TAILQ_FIRST(&txq_queue->head);
    TAILQ_REMOVE(&txq_queue->head, next_tq, list_elem);

    credit = OL_A_MIN(credit, TX_SCH_MAX_CREDIT_FOR_THIS_TID(next_tq));
    frames = next_tq->frms; /* download as many frames as credit allows */
    tx_limit = ol_tx_bad_peer_dequeue_check(next_tq, frames, &tx_limit_flag);
    frames = ol_tx_dequeue(
	pdev, next_tq, &sctx->head, tx_limit, &credit, &bytes);
    ol_tx_bad_peer_update_tx_limit(pdev, next_tq, frames, tx_limit_flag);

    used_credits = credit;
    txq_queue->frms -= frames;
    txq_queue->bytes -= bytes;

    if (next_tq->frms > 0) {
        TAILQ_INSERT_TAIL(&txq_queue->head, next_tq, list_elem);
        TAILQ_INSERT_TAIL(
            &scheduler->tx_active_tids_list, txq_queue, list_elem);
        txq_queue->active = TRUE;
    } else if (!TAILQ_EMPTY(&txq_queue->head)) {
        /*
         * This tx queue is empty, but there's another tx queue for the
         * same TID that is not empty.  Thus, the TID as a whole is active.
         */
        TAILQ_INSERT_TAIL(
            &scheduler->tx_active_tids_list, txq_queue, list_elem);
        txq_queue->active = TRUE;
    }
    sctx->frms += frames;

    TX_SCHED_DEBUG_PRINT("Leave %s\n", __func__);
    return used_credits;
}

static inline void
ol_tx_sched_txq_enqueue_rr(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int tid,
    int frms,
    int bytes)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;

    txq_queue = &scheduler->tx_active_queues_in_tid_array[tid];
    if (txq->flag != ol_tx_queue_active)
    {
        TAILQ_INSERT_TAIL(&txq_queue->head, txq, list_elem);
    }
    txq_queue->frms += frms;
    txq_queue->bytes += bytes;

    if (!txq_queue->active) {
        TAILQ_INSERT_TAIL(
            &scheduler->tx_active_tids_list, txq_queue, list_elem);
        txq_queue->active = TRUE;
    }
}

static inline void
ol_tx_sched_txq_deactivate_rr(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int tid)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;

    txq_queue = &scheduler->tx_active_queues_in_tid_array[tid];
    txq_queue->frms -= txq->frms;
    txq_queue->bytes -= txq->bytes;

    TAILQ_REMOVE(&txq_queue->head, txq, list_elem);
    //if (txq_queue->frms == 0 && txq_queue->active) {
    if (TAILQ_EMPTY(&txq_queue->head) && txq_queue->active) {
        TAILQ_REMOVE(&scheduler->tx_active_tids_list, txq_queue, list_elem);
        txq_queue->active = FALSE;
    }
}

ol_tx_frms_queue_list *
ol_tx_sched_category_tx_queues_rr(struct ol_txrx_pdev_t *pdev, int tid)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;

    txq_queue = &scheduler->tx_active_queues_in_tid_array[tid];
    return &txq_queue->head;
}

int
ol_tx_sched_discard_select_category_rr(struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_rr_t *scheduler;
    u_int8_t i, tid = 0;
    int max_score = 0;

    scheduler = pdev->tx_sched.scheduler;
    /*
     * Choose which TID's tx frames to drop next based on two factors:
     * 1.  Which TID has the most tx frames present
     * 2.  The TID's priority (high-priority TIDs have a low discard_weight)
     */
    for (i = 0; i < (OL_TX_NUM_TIDS + OL_TX_VDEV_NUM_QUEUES); i++) {
        int score;
        score =
            scheduler->tx_active_queues_in_tid_array[i].frms *
            scheduler->discard_weights[i];
        if (max_score == 0 || score > max_score) {
            max_score = score;
            tid = i;
        }
    }
    return tid;
}

void
ol_tx_sched_txq_discard_rr(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int tid, int frames, int bytes)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;

    txq_queue = &scheduler->tx_active_queues_in_tid_array[tid];

    if (0 == txq->frms) {
        TAILQ_REMOVE(&txq_queue->head, txq, list_elem);
    }

    txq_queue->frms -= frames;
    txq_queue->bytes -= bytes;
    if (txq_queue->active == TRUE && txq_queue->frms == 0) {
        TAILQ_REMOVE(&scheduler->tx_active_tids_list, txq_queue, list_elem);
        txq_queue->active = FALSE;
    }
}

void
ol_tx_sched_category_info_rr(
    struct ol_txrx_pdev_t *pdev, int cat, int *active, int *frms, int *bytes)
{
    struct ol_tx_sched_rr_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_active_queues_in_tid_t *txq_queue;

    txq_queue = &scheduler->tx_active_queues_in_tid_array[cat];

    *active = txq_queue->active;
    *frms = txq_queue->frms;
    *bytes = txq_queue->bytes;
}

enum {
    ol_tx_sched_discard_weight_voice = 1,
    ol_tx_sched_discard_weight_video = 4,
    ol_tx_sched_discard_weight_ucast_default = 8,
    ol_tx_sched_discard_weight_mgmt_non_qos = 1, /* 0? */
    ol_tx_sched_discard_weight_mcast = 1, /* 0? also for probe & assoc */
};

void *
ol_tx_sched_init_rr(
  struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_rr_t *scheduler;
    int i;

    scheduler = adf_os_mem_alloc(pdev->osdev, sizeof(struct ol_tx_sched_rr_t));
    if (scheduler == NULL) {
        return scheduler;
    }

    for (i = 0; i < (OL_TX_NUM_TIDS + OL_TX_VDEV_NUM_QUEUES); i++) {
        scheduler->tx_active_queues_in_tid_array[i].tid = i;
        TAILQ_INIT(&scheduler->tx_active_queues_in_tid_array[i].head);
        scheduler->tx_active_queues_in_tid_array[i].active = 0;
        scheduler->tx_active_queues_in_tid_array[i].frms = 0;
        scheduler->tx_active_queues_in_tid_array[i].bytes = 0;
    }
    for (i = 0; i < OL_TX_NUM_TIDS; i++) {
        scheduler->tx_active_queues_in_tid_array[i].tid = i;
        if (i < OL_TX_NON_QOS_TID) {
            int ac = TXRX_TID_TO_WMM_AC(i);
            switch (ac) {
            case TXRX_WMM_AC_VO:
                scheduler->discard_weights[i] =
                    ol_tx_sched_discard_weight_voice;
            case TXRX_WMM_AC_VI:
                scheduler->discard_weights[i] =
                    ol_tx_sched_discard_weight_video;
            default:
                scheduler->discard_weights[i] =
                    ol_tx_sched_discard_weight_ucast_default;
            };
        } else {
            scheduler->discard_weights[i] =
                ol_tx_sched_discard_weight_mgmt_non_qos;
        }
    }
    for (i = 0; i < OL_TX_VDEV_NUM_QUEUES; i++) {
        int j = i + OL_TX_NUM_TIDS;
        scheduler->tx_active_queues_in_tid_array[j].tid = OL_TX_NUM_TIDS - 1;
        scheduler->discard_weights[j] = ol_tx_sched_discard_weight_mcast;
    }
    TAILQ_INIT(&scheduler->tx_active_tids_list);

    return scheduler;
}

void
ol_txrx_set_wmm_param(ol_txrx_pdev_handle data_pdev, struct ol_tx_wmm_param_t wmm_param)
{
    VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO_LOW,
        "Dummy function when OL_TX_SCHED_RR is enabled\n");
}

/**
 * ol_tx_sched_stats_display() - tx sched stats display
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_stats_display(struct ol_txrx_pdev_t *pdev)
{
}

/**
 * ol_tx_sched_cur_state_display() - tx sched cur stat display
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_cur_state_display(struct ol_txrx_pdev_t *pdev)
{
}

/**
 * ol_tx_sched_cur_state_display() - reset tx sched stats
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_stats_clear(struct ol_txrx_pdev_t *pdev)
{
}

#endif /* OL_TX_SCHED == OL_TX_SCHED_RR */

/*--- advanced scheduler ----------------------------------------------------*/
#if OL_TX_SCHED == OL_TX_SCHED_WRR_ADV

/*--- definitions ---*/

struct ol_tx_sched_wrr_adv_category_info_t {
    struct {
        int wrr_skip_weight;
        u_int32_t credit_threshold;
        u_int16_t send_limit;
        int credit_reserve;
        int discard_weight;
    } specs;
    struct {
        int wrr_count;
        int frms;
        int bytes;
        ol_tx_frms_queue_list head;
        bool active;
    } state;
#ifdef DEBUG_HL_LOGGING
    struct
    {
        char *cat_name;
        unsigned int queued;
        unsigned int dispatched;
        unsigned int discard;
    } stat;
#endif
};

#define OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(cat, \
    wrr_skip_weight, \
    credit_threshold, \
    send_limit, \
    credit_reserve, \
    discard_weights) \
        enum { OL_TX_SCHED_WRR_ADV_ ## cat ## _WRR_SKIP_WEIGHT = \
            (wrr_skip_weight) }; \
        enum { OL_TX_SCHED_WRR_ADV_ ## cat ## _CREDIT_THRESHOLD = \
            (credit_threshold) }; \
        enum { OL_TX_SCHED_WRR_ADV_ ## cat ## _SEND_LIMIT = \
            (send_limit) }; \
        enum { OL_TX_SCHED_WRR_ADV_ ## cat ## _CREDIT_RESERVE = \
            (credit_reserve) }; \
        enum { OL_TX_SCHED_WRR_ADV_ ## cat ## _DISCARD_WEIGHT = \
            (discard_weights) }

/* Rome:
 * For high-volume traffic flows (VI, BE, BK), use a credit threshold
 * roughly equal to a large A-MPDU (occupying half the target memory
 * available for holding tx frames) to download AMPDU-sized batches
 * of traffic.
 * For high-priority, low-volume traffic flows (VO and mgmt), use no
 * credit threshold, to minimize download latency.
 */
//                                            WRR           send
//                                           skip  credit  limit credit disc
//                                            wts  thresh (frms) reserv  wts
#ifdef HIF_SDIO
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(VO,           1,     17,    24,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(VI,           3,     17,    16,     1,  4);
#if defined (MSM8976_TCP_PERF)
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(BE,          10,      9,     8,     1,  8);
#else
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(BE,          10,     17,    16,     1,  8);
#endif
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(BK,          12,      6,     6,     1,  8);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(NON_QOS_DATA,10,     17,    16,     1,  8);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(UCAST_MGMT,   1,      1,     4,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_DATA,  10,     17,     4,     1,  4);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_MGMT,   1,      1,     4,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_VO,     1,      17,   24,     0,  1);
#else
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(VO,           1,     16,    24,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(VI,           3,     16,    16,     1,  4);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(BE,          10,     12,    12,     1,  8);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(BK,          12,      6,     6,     1,  8);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(NON_QOS_DATA,12,      6,     4,     1,  8);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(UCAST_MGMT,   1,      1,     4,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_DATA,  10,     16,     4,     1,  4);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_MGMT,   1,      1,     4,     0,  1);
OL_TX_SCHED_WRR_ADV_CAT_CFG_SPEC(MCAST_VO,     1,     17,    24,     0,  1);
#endif

#ifdef DEBUG_HL_LOGGING

#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INIT(category, scheduler)                                  \
    do {                                                                                        \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category].stat.queued = 0;            \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category].stat.discard = 0;           \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category].stat.dispatched = 0;        \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category].stat.cat_name = #category;  \
    } while (0)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_QUEUED(category, frms)             \
        category->stat.queued += frms
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISCARD(category, frms)            \
        category->stat.discard += frms
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISPATCHED(category, frms)         \
        category->stat.dispatched += frms
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_DUMP(scheduler)                        \
        ol_tx_sched_wrr_adv_cat_stat_dump(scheduler)
#define OL_TX_SCHED_WRR_ADV_CAT_CUR_STATE_DUMP(scheduler)                   \
        ol_tx_sched_wrr_adv_cat_cur_state_dump(scheduler)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_CLEAR(scheduler)                       \
        ol_tx_sched_wrr_adv_cat_stat_clear(scheduler)

#else   /* DEBUG_HL_LOGGING */

#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INIT(category, scheduler)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_QUEUED(category, frms)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISCARD(category, frms)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISPATCHED(category, frms)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_DUMP(scheduler)
#define OL_TX_SCHED_WRR_ADV_CAT_CUR_STATE_DUMP(scheduler)
#define OL_TX_SCHED_WRR_ADV_CAT_STAT_CLEAR(scheduler)

#endif  /* DEBUG_HL_LOGGING */

#define OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(category, scheduler) \
    do { \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category] \
            .specs.wrr_skip_weight = \
            OL_TX_SCHED_WRR_ADV_ ## category ## _WRR_SKIP_WEIGHT; \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category] \
            .specs.credit_threshold = \
            OL_TX_SCHED_WRR_ADV_ ## category ## _CREDIT_THRESHOLD; \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category] \
            .specs.send_limit = \
            OL_TX_SCHED_WRR_ADV_ ## category ## _SEND_LIMIT; \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category] \
            .specs.credit_reserve = \
            OL_TX_SCHED_WRR_ADV_ ## category ## _CREDIT_RESERVE; \
        scheduler->categories[OL_TX_SCHED_WRR_ADV_CAT_ ## category] \
            .specs.discard_weight = \
            OL_TX_SCHED_WRR_ADV_ ## category ## _DISCARD_WEIGHT; \
        OL_TX_SCHED_WRR_ADV_CAT_STAT_INIT(category, scheduler); \
    } while (0)

struct ol_tx_sched_wrr_adv_t {
    int order[OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES];
    int index;
    struct ol_tx_sched_wrr_adv_category_info_t
        categories[OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES];
};

#define OL_TX_AIFS_DEFAULT_VO   2
#define OL_TX_AIFS_DEFAULT_VI   2
#define OL_TX_AIFS_DEFAULT_BE   3
#define OL_TX_AIFS_DEFAULT_BK   7
#define OL_TX_CW_MIN_DEFAULT_VO   3
#define OL_TX_CW_MIN_DEFAULT_VI   7
#define OL_TX_CW_MIN_DEFAULT_BE   15
#define OL_TX_CW_MIN_DEFAULT_BK   15

/*--- functions ---*/

#ifdef DEBUG_HL_LOGGING
static void ol_tx_sched_wrr_adv_cat_stat_dump
                         (struct ol_tx_sched_wrr_adv_t *scheduler)
{
    int i;
    VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,"Scheduler Stats:");
    VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
              "====category(CRR,CRT,WSW):  Queued  Discard  Dequeued  frms  wrr===");
    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; ++ i) {
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
                  "%12s(%2d, %2d, %2d):  %6d  %7d  %8d  %4d  %3d",
                scheduler->categories[i].stat.cat_name,
                scheduler->categories[i].specs.credit_reserve,
                scheduler->categories[i].specs.credit_threshold,
                scheduler->categories[i].specs.wrr_skip_weight,
                scheduler->categories[i].stat.queued,
                scheduler->categories[i].stat.discard,
                scheduler->categories[i].stat.dispatched,
                scheduler->categories[i].state.frms,
                scheduler->categories[i].state.wrr_count);
    }
}

static void ol_tx_sched_wrr_adv_cat_cur_state_dump
                       (struct ol_tx_sched_wrr_adv_t *scheduler)
{
    int i;
    VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,"Scheduler State Snapshot:");
    VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
              "====category(CRR,CRT,WSW):  IS_Active  Pend_Frames  Pend_bytes  wrr===");
    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; ++ i) {
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
                  "%12s(%2d, %2d, %2d):  %9d  %11d  %10d  %3d",
                scheduler->categories[i].stat.cat_name,
                scheduler->categories[i].specs.credit_reserve,
                scheduler->categories[i].specs.credit_threshold,
                scheduler->categories[i].specs.wrr_skip_weight,
                scheduler->categories[i].state.active,
                scheduler->categories[i].state.frms,
                scheduler->categories[i].state.bytes,
                scheduler->categories[i].state.wrr_count);
    }
}

static void ol_tx_sched_wrr_adv_cat_stat_clear
                       (struct ol_tx_sched_wrr_adv_t *scheduler)
{
    int i;
    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; ++ i) {
        scheduler->categories[i].stat.queued = 0;
        scheduler->categories[i].stat.discard = 0;
        scheduler->categories[i].stat.dispatched = 0;
    }
}

#endif

static void
ol_tx_sched_select_init_wrr_adv(struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    /* start selection from the front of the ordered list */
    scheduler->index = 0;
    pdev->tx_sched.last_used_txq = NULL;
}

static void
ol_tx_sched_wrr_adv_rotate_order_list_tail(
    struct ol_tx_sched_wrr_adv_t *scheduler, int idx)
{
    int value;
    /* remember the value of the specified element */
    value = scheduler->order[idx];
    /* shift all further elements up one space */
    for (; idx < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES-1; idx++) {
        scheduler->order[idx] = scheduler->order[idx + 1];
    }
    /* put the specified element at the end */
    scheduler->order[idx] = value;
}

static void
ol_tx_sched_wrr_adv_credit_sanity_check(struct ol_txrx_pdev_t *pdev, u_int32_t credit)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    int i;
    int okay = 1;

    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; i++) {
        if (scheduler->categories[i].specs.credit_threshold > credit) {
            VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
                "*** Config error: credit (%d) not enough "
                "to support category %d threshold (%d)\n",
                credit, i, scheduler->categories[i].specs.credit_threshold);
             okay = 0;
        }
    }
    adf_os_assert(okay);
}

#if defined(AUDIO_MULTICAST_AGGR_SUPPORT)
#define OL_TX_VO_MCAST_SET_START(tag)	(*(tag) |= 0x01)
#define OL_TX_VO_MCAST_SET_END(tag)	(*(tag) |= 0x02)
static void
ol_tx_sched_wrr_adv_vo_mcast_addtag(struct ol_tx_frms_queue_t *txq,
				    int tx_limit)
{
	int tx_cnt = 0;
	struct ol_tx_desc_t *tx_desc, *prev_desc;
	adf_nbuf_t buf;
	u_int8_t group_id;
	u_int8_t *tag, *prev_tag = NULL;
	u_int32_t offset;

	prev_desc = TAILQ_FIRST(&txq->head);
	TAILQ_FOREACH(tx_desc, &txq->head, tx_desc_list_elem) {
		group_id = tx_desc->group_id;
		buf = tx_desc->netbuf;
		offset = tx_desc->tag_offset;

		if (offset + OL_TX_VO_MCAST_TAG_SIZE > adf_nbuf_len(buf)) {
			VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
				  "Invalid audio multicast tag offset!");
			break;
		}

		tag = adf_nbuf_data(buf) + offset;
		*tag = 0;

		if (tx_cnt == 0) {
			OL_TX_VO_MCAST_SET_START(tag);
		} else if (tx_desc->group_id != prev_desc->group_id) {
			OL_TX_VO_MCAST_SET_START(tag);
			OL_TX_VO_MCAST_SET_END(prev_tag);
		}

		prev_tag = tag;

		if (++tx_cnt == tx_limit)
			break;

		prev_desc = tx_desc;
	}

	if (prev_tag)
		OL_TX_VO_MCAST_SET_END(prev_tag);
}

static int ol_tx_sched_wrr_adv_vo_mcast_aggr(struct ol_tx_frms_queue_t *txq,
					     int category_index,
					     u_int32_t credit,
					     int tx_limit)
{
	u_int64_t expire_time, tsf;
	struct ol_tx_desc_t *first_tx_desc, *tx_desc;
	u_int8_t total_num, group_id, seqno;
	struct ol_txrx_vdev_t *vdev;
	struct ol_audio_multicast_aggr_conf *conf;
	int tx_allow_cnt = 0, force_tx = 0;

	if (category_index != OL_TX_SCHED_WRR_ADV_CAT_MCAST_VO ||
	    !txq->frms || !credit || !tx_limit)
		return tx_limit;

	first_tx_desc = TAILQ_FIRST(&txq->head);
	vdev = first_tx_desc->vdev;
	conf = &vdev->au_mcast_conf;

	/* expired, flush as no subsequence packets */
	expire_time = txq->last_enq_time +
		msecs_to_jiffies(OL_TX_VO_MCAST_GROUP_EXP_TIME);
	if (vos_system_time_after(jiffies, expire_time)) {
		conf->expire_error++;
		force_tx = 1;
	}

	total_num = first_tx_desc->total_num;
	seqno = first_tx_desc->seqno;
	tsf = first_tx_desc->tsf;
	group_id = first_tx_desc->group_id;

	if (seqno != 1)
		conf->seqno_error++;

	TAILQ_FOREACH(tx_desc, &txq->head, tx_desc_list_elem) {
		if (tx_desc->tsf != tsf) {
			force_tx = 1;
			break;
		}

		if (++tx_allow_cnt == total_num)
			break;
	}

	if (tx_allow_cnt == total_num || force_tx)
		tx_limit = tx_allow_cnt > credit ? 0 : tx_allow_cnt;
	else
		tx_limit = 0;

	if (!tx_limit)
		goto out;

	ol_tx_sched_wrr_adv_vo_mcast_addtag(txq, tx_limit);

	conf->packet_success += tx_limit;
	conf->sched_count++;
out:
	return tx_limit;
}
#else
static int ol_tx_sched_wrr_adv_vo_mcast_aggr(struct ol_tx_frms_queue_t *txq,
					     int category_index,
					     u_int32_t credit,
					     int tx_limit)
{
	return tx_limit;
}
#endif

/*
 * The scheduler sync spinlock has been acquired outside this function,
 * so there is no need to worry about mutex within this function.
 */
static int
ol_tx_sched_select_batch_wrr_adv(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_sched_ctx *sctx,
    u_int32_t credit)
{
    static int first = 1;
    int category_index = 0;
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_frms_queue_t *txq;
    int index;
    struct ol_tx_sched_wrr_adv_category_info_t *category = NULL;
    int frames, bytes, used_credits = 0, tx_limit;
    u_int16_t tx_limit_flag;
    /*
     * Just for good measure, do a sanity check that the initial credit
     * is enough to cover every category's credit threshold.
     */
    if (first) {
        first = 0;
        ol_tx_sched_wrr_adv_credit_sanity_check(pdev, credit);
    }

    /* choose the traffic category from the ordered list */
    index = scheduler->index;
    while (index < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES) {
        category_index = scheduler->order[index];
        category = &scheduler->categories[category_index];
        if (!category->state.active) {
            /* move on to the next category */
            index++;
            continue;
        }
        if (++category->state.wrr_count < category->specs.wrr_skip_weight) {
            /* skip this cateogry (move it to the back) */
            ol_tx_sched_wrr_adv_rotate_order_list_tail(scheduler, index);
            /* try again (iterate) on the new element that was moved up */
            continue;
        }
        /* found the first active category whose WRR turn is present */
        break;
    }
    if (index >= OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES) {
        /* no categories are active */
        return 0;
    }

    /* is there enough credit for the selected category? */
    if (credit < category->specs.credit_threshold) {
        /*
         * Can't send yet - wait until more credit becomes available.
         * In the meantime, restore the WRR counter (since we didn't
         * service this category after all).
         */
        category->state.wrr_count = category->state.wrr_count - 1;
        return 0;
    }
    /* enough credit is available - go ahead and send some frames */
    /*
     * This category was serviced - reset the WRR counter, and move this
     * category to the back of the order list.
     */
    category->state.wrr_count = 0;
    ol_tx_sched_wrr_adv_rotate_order_list_tail(scheduler, index);
    /*
     * With this category moved to the back, if there's still any credit
     * left, set up the next invocation of this function to start from
     * where this one left off, by looking at the category that just got
     * shifted forward into the position the service category was
     * occupying.
     */
    scheduler->index = index;

    /*
     * Take the tx queue from the head of the category list.
     */
    txq = TAILQ_FIRST(&category->state.head);

    if (txq){
        TAILQ_REMOVE(&category->state.head, txq, list_elem);
        credit = OL_TX_TXQ_GROUP_CREDIT_LIMIT(pdev, txq, credit);
        if (credit > category->specs.credit_reserve) {
            credit -= category->specs.credit_reserve;
            /*
             * this tx queue will download some frames,
             * so update last_used_txq
             */
            pdev->tx_sched.last_used_txq = txq;

            tx_limit = ol_tx_bad_peer_dequeue_check(txq, category->specs.send_limit, &tx_limit_flag);

	    tx_limit = ol_tx_sched_wrr_adv_vo_mcast_aggr(txq, category_index, credit, tx_limit);

            frames = ol_tx_dequeue(
                pdev, txq, &sctx->head, tx_limit, &credit, &bytes);
            ol_tx_bad_peer_update_tx_limit(pdev, txq, frames, tx_limit_flag);

            OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISPATCHED(category, frames);
            used_credits = credit;
            category->state.frms -= frames;
            category->state.bytes -= bytes;
            if (txq->frms > 0) {
                TAILQ_INSERT_TAIL(&category->state.head, txq, list_elem);
            } else {
                if (category->state.frms == 0) {
                    category->state.active = 0;
                }
            }
            sctx->frms += frames;
            OL_TX_TXQ_GROUP_CREDIT_UPDATE(pdev, txq, -credit, 0);
        } else {
            if (OL_TX_IS_TXQ_LAST_SERVICED_QUEUE(pdev, txq)) {
                /*
                 * The scheduler has looked at all the active tx queues
                 * but none were able to download any of their tx frames.
                 * Nothing is changed, so if none were able to download before,
                 * they wont be able to download now.
                 * Return that no credit has been used, which
                 * will cause the scheduler to stop.
                 */
                TAILQ_INSERT_HEAD(&category->state.head, txq, list_elem);
                return 0;
            } else {
                TAILQ_INSERT_TAIL(&category->state.head, txq, list_elem);
                if (!pdev->tx_sched.last_used_txq) {
                    pdev->tx_sched.last_used_txq = txq;
                }
            }
        }
        TX_SCHED_DEBUG_PRINT("Leave %s\n", __func__);
    } else {
        used_credits = 0;
        /* TODO: find its reason */
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
             "ol_tx_sched_select_batch_wrr_adv: error, no TXQ can be popped.");
    }
    return used_credits;
}

static inline void
ol_tx_sched_txq_enqueue_wrr_adv(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int tid,
    int frms,
    int bytes)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_sched_wrr_adv_category_info_t *category;

    category = &scheduler->categories[pdev->tid_to_ac[tid]];
    category->state.frms += frms;
    category->state.bytes += bytes;
    OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_QUEUED(category, frms);
    if (txq->flag != ol_tx_queue_active)
    {
        TAILQ_INSERT_TAIL(&category->state.head, txq, list_elem);
        category->state.active = 1; /* may have already been active */
    }
}

static inline void
ol_tx_sched_txq_deactivate_wrr_adv(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int tid)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_sched_wrr_adv_category_info_t *category;

    category = &scheduler->categories[pdev->tid_to_ac[tid]];
    category->state.frms -= txq->frms;
    category->state.bytes -= txq->bytes;

    TAILQ_REMOVE(&category->state.head, txq, list_elem);

    if (category->state.frms == 0 && category->state.active) {
        category->state.active = 0;
    }
}

ol_tx_frms_queue_list *
ol_tx_sched_category_tx_queues_wrr_adv(struct ol_txrx_pdev_t *pdev, int cat)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_sched_wrr_adv_category_info_t *category;

    category = &scheduler->categories[cat];
    return &category->state.head;
}

int
ol_tx_sched_discard_select_category_wrr_adv(struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_wrr_adv_t *scheduler;
    u_int8_t i, cat = 0;
    int max_score = 0;

    scheduler = pdev->tx_sched.scheduler;
    /*
     * Choose which category's tx frames to drop next based on two factors:
     * 1.  Which category has the most tx frames present
     * 2.  The category's priority (high-priority categories have a low
     *     discard_weight)
     */
    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; i++) {
        int score;
        score =
            scheduler->categories[i].state.frms *
            scheduler->categories[i].specs.discard_weight;
        if (max_score == 0 || score > max_score) {
            max_score = score;
            cat = i;
        }
    }
    return cat;
}

void
ol_tx_sched_txq_discard_wrr_adv(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_frms_queue_t *txq,
    int cat, int frames, int bytes)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_sched_wrr_adv_category_info_t *category;

    category = &scheduler->categories[cat];

    if (0 == txq->frms) {
        TAILQ_REMOVE(&category->state.head, txq, list_elem);
    }

    category->state.frms -= frames;
    category->state.bytes -= bytes;
    OL_TX_SCHED_WRR_ADV_CAT_STAT_INC_DISCARD(category, frames);
    if (category->state.frms == 0) {
        category->state.active = 0;
    }
}

void
ol_tx_sched_category_info_wrr_adv(
    struct ol_txrx_pdev_t *pdev, int cat, int *active, int *frms, int *bytes)
{
    struct ol_tx_sched_wrr_adv_t *scheduler = pdev->tx_sched.scheduler;
    struct ol_tx_sched_wrr_adv_category_info_t *category;

    category = &scheduler->categories[cat];
    *active = category->state.active;
    *frms = category->state.frms;
    *bytes = category->state.bytes;
}


/**
 * ol_tx_sched_wrr_param_update() - update the WRR TX sched params
 * @pdev: Pointer to PDEV structure.
 * @scheduler: Pointer to tx scheduler.
 *
 * Update the WRR TX schedule parameters for each category if it is
 * specified in the ini file by user.
 *
 * Return: none
 */
void ol_tx_sched_wrr_param_update(struct ol_txrx_pdev_t *pdev,
				struct ol_tx_sched_wrr_adv_t *scheduler)
{
	int i;
#ifdef WLAN_DEBUG
	char *tx_sched_wrr_name[4] = {
		"BE",
		"BK",
		"VI",
		"VO"
	};
#endif

	if (NULL == scheduler)
		return;

	VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO,
		"%s: Tuning the TX scheduler wrr parameters by ini file:",
		__func__);
	VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO,
		"         skip credit limit credit disc");

	for (i = OL_TX_SCHED_WRR_ADV_CAT_BE;
		i <= OL_TX_SCHED_WRR_ADV_CAT_VO; i++) {
		if (ol_cfg_get_wrr_skip_weight(pdev->ctrl_pdev, i)) {
			scheduler->categories[i].specs.wrr_skip_weight =
				ol_cfg_get_wrr_skip_weight(pdev->ctrl_pdev, i);
			scheduler->categories[i].specs.credit_threshold =
				ol_cfg_get_credit_threshold(pdev->ctrl_pdev, i);
			scheduler->categories[i].specs.send_limit =
				ol_cfg_get_send_limit(pdev->ctrl_pdev, i);
			scheduler->categories[i].specs.credit_reserve =
				ol_cfg_get_credit_reserve(pdev->ctrl_pdev, i);
			scheduler->categories[i].specs.discard_weight =
				ol_cfg_get_discard_weight(pdev->ctrl_pdev, i);

			VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO,
				"%s-update: %d,  %d,    %d,   %d,    %d",
				tx_sched_wrr_name[i],
				scheduler->categories[i].specs.wrr_skip_weight,
				scheduler->categories[i].specs.credit_threshold,
				scheduler->categories[i].specs.send_limit,
				scheduler->categories[i].specs.credit_reserve,
				scheduler->categories[i].specs.discard_weight);
		} else {
			VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO,
				"%s-orig: %d,  %d,    %d,   %d,    %d",
				tx_sched_wrr_name[i],
				scheduler->categories[i].specs.wrr_skip_weight,
				scheduler->categories[i].specs.credit_threshold,
				scheduler->categories[i].specs.send_limit,
				scheduler->categories[i].specs.credit_reserve,
				scheduler->categories[i].specs.discard_weight);
		}
	}
}

void *
ol_tx_sched_init_wrr_adv(
  struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_wrr_adv_t *scheduler;
    int i;

    scheduler = adf_os_mem_alloc(
        pdev->osdev, sizeof(struct ol_tx_sched_wrr_adv_t));
    if (scheduler == NULL) {
        return scheduler;
    }
    adf_os_mem_zero(scheduler, sizeof(*scheduler));

    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(VO, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(VI, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(BE, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(BK, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(NON_QOS_DATA, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(UCAST_MGMT, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(MCAST_DATA, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(MCAST_MGMT, scheduler);
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(MCAST_VO, scheduler);

    ol_tx_sched_wrr_param_update(pdev, scheduler);

    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; i++) {
        scheduler->categories[i].state.active = 0;
        scheduler->categories[i].state.frms = 0;
        //scheduler->categories[i].state.bytes = 0;
        TAILQ_INIT(&scheduler->categories[i].state.head);
        /* init categories to not be skipped before their initial selection */
        scheduler->categories[i].state.wrr_count =
            scheduler->categories[i].specs.wrr_skip_weight - 1;
    }

    /*
     * Init the order array - the initial ordering doesn't matter, as the
     * order array will get reshuffled as data arrives.
     */
    for (i = 0; i < OL_TX_SCHED_WRR_ADV_NUM_CATEGORIES; i++) {
        scheduler->order[i] = i;
    }

    return scheduler;
}


/* WMM parameters are suppposed to be passed when associate with AP.
 * According to AIFS+CWMin, the function maps each queue to one of four default
 * settings of the scheduler, ie. VO, VI, BE, or BK.
 */
void
ol_txrx_set_wmm_param(ol_txrx_pdev_handle data_pdev, struct ol_tx_wmm_param_t wmm_param)
{
    struct ol_tx_sched_wrr_adv_t def_cfg;
    struct ol_tx_sched_wrr_adv_t *scheduler = data_pdev->tx_sched.scheduler;
    u_int32_t i, ac_selected, weight[OL_TX_NUM_WMM_AC], default_edca[OL_TX_NUM_WMM_AC];

    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(VO, (&def_cfg));
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(VI, (&def_cfg));
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(BE, (&def_cfg));
    OL_TX_SCHED_WRR_ADV_CAT_CFG_STORE(BK, (&def_cfg));

    // default_eca = AIFS + CWMin
    default_edca[OL_TX_SCHED_WRR_ADV_CAT_VO] =
                      OL_TX_AIFS_DEFAULT_VO + OL_TX_CW_MIN_DEFAULT_VO;
    default_edca[OL_TX_SCHED_WRR_ADV_CAT_VI] =
                      OL_TX_AIFS_DEFAULT_VI + OL_TX_CW_MIN_DEFAULT_VI;
    default_edca[OL_TX_SCHED_WRR_ADV_CAT_BE] =
                      OL_TX_AIFS_DEFAULT_BE + OL_TX_CW_MIN_DEFAULT_BE;
    default_edca[OL_TX_SCHED_WRR_ADV_CAT_BK] =
                      OL_TX_AIFS_DEFAULT_BK + OL_TX_CW_MIN_DEFAULT_BK;

    weight[OL_TX_SCHED_WRR_ADV_CAT_VO] =
             wmm_param.ac[OL_TX_WMM_AC_VO].aifs + wmm_param.ac[OL_TX_WMM_AC_VO].cwmin;
    weight[OL_TX_SCHED_WRR_ADV_CAT_VI] =
             wmm_param.ac[OL_TX_WMM_AC_VI].aifs + wmm_param.ac[OL_TX_WMM_AC_VI].cwmin;
    weight[OL_TX_SCHED_WRR_ADV_CAT_BK] =
             wmm_param.ac[OL_TX_WMM_AC_BK].aifs + wmm_param.ac[OL_TX_WMM_AC_BK].cwmin;
    weight[OL_TX_SCHED_WRR_ADV_CAT_BE] =
             wmm_param.ac[OL_TX_WMM_AC_BE].aifs + wmm_param.ac[OL_TX_WMM_AC_BE].cwmin;

    for (i = 0; i < OL_TX_NUM_WMM_AC; i++) {

        if (default_edca[OL_TX_SCHED_WRR_ADV_CAT_VO] >= weight[i]) {
            ac_selected = OL_TX_SCHED_WRR_ADV_CAT_VO;
        } else if (default_edca[OL_TX_SCHED_WRR_ADV_CAT_VI] >= weight[i]) {
            ac_selected = OL_TX_SCHED_WRR_ADV_CAT_VI;
        } else if (default_edca[OL_TX_SCHED_WRR_ADV_CAT_BE] >= weight[i]) {
            ac_selected = OL_TX_SCHED_WRR_ADV_CAT_BE;
        } else {
            ac_selected = OL_TX_SCHED_WRR_ADV_CAT_BK;
        }

        scheduler->categories[i].specs.wrr_skip_weight =
                                 def_cfg.categories[ac_selected].specs.wrr_skip_weight;
        scheduler->categories[i].specs.credit_threshold =
                                 def_cfg.categories[ac_selected].specs.credit_threshold;
        scheduler->categories[i].specs.send_limit =
                                 def_cfg.categories[ac_selected].specs.send_limit;
        scheduler->categories[i].specs.credit_reserve =
                                 def_cfg.categories[ac_selected].specs.credit_reserve;
        scheduler->categories[i].specs.discard_weight =
                                 def_cfg.categories[ac_selected].specs.discard_weight;
    }
}

/**
 * ol_tx_sched_stats_display() - tx sched stats display
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_stats_display(struct ol_txrx_pdev_t *pdev)
{
    OL_TX_SCHED_WRR_ADV_CAT_STAT_DUMP(pdev->tx_sched.scheduler);
}

/**
 * ol_tx_sched_cur_state_display() - tx sched cur stat display
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_cur_state_display(struct ol_txrx_pdev_t *pdev)
{
    OL_TX_SCHED_WRR_ADV_CAT_CUR_STATE_DUMP(pdev->tx_sched.scheduler);
}

/**
 * ol_tx_sched_cur_state_display() - reset tx sched stats
 * @pdev: Pointer to the PDEV structure.
 *
 * Return: none.
 */
void ol_tx_sched_stats_clear(struct ol_txrx_pdev_t *pdev)
{
    OL_TX_SCHED_WRR_ADV_CAT_STAT_CLEAR(pdev->tx_sched.scheduler);
}

#endif /* OL_TX_SCHED == OL_TX_SCHED_WRR_ADV */

/*--- congestion control discard --------------------------------------------*/

struct ol_tx_frms_queue_t *
ol_tx_sched_discard_select_txq(
    struct ol_txrx_pdev_t *pdev,
    ol_tx_frms_queue_list *tx_queues)
{
    struct ol_tx_frms_queue_t *txq;
    struct ol_tx_frms_queue_t *selected_txq = NULL;
    int max_frms = 0;

    /* return the tx queue with the most frames */
    TAILQ_FOREACH(txq, tx_queues, list_elem) {
        if (txq->frms > max_frms) {
            max_frms = txq->frms;
            selected_txq = txq;
        }
    }
    return selected_txq;
}

u_int16_t
ol_tx_sched_discard_select(
    struct ol_txrx_pdev_t *pdev,
    u_int16_t frms,
    ol_tx_desc_list *tx_descs,
    a_bool_t force)
{
    int cat;
    struct ol_tx_frms_queue_t *txq;
    int bytes;
    u_int32_t credit;
    struct ol_tx_sched_notify_ctx_t notify_ctx;

    /* first decide what category of traffic (e.g. TID or AC) to discard next */
    cat = ol_tx_sched_discard_select_category(pdev);

    /* then decide which peer within this category to discard from next */
    txq = ol_tx_sched_discard_select_txq(
        pdev, ol_tx_sched_category_tx_queues(pdev, cat));
    if (NULL == txq)
    {
        /* No More pending Tx Packets in Tx Queue. Exit Discard loop */
        return 0;
    }

    if (force == A_FALSE) {
        /*
         * Now decide how many frames to discard from this peer-TID.
         * Don't discard more frames than the caller has specified.
         * Don't discard more than a fixed quantum of frames at a time.
         * Don't discard more than 50% of the queue's frames at a time,
         * but if there's only 1 frame left, go ahead and discard it.
         */
        #define OL_TX_DISCARD_QUANTUM 10
        if (OL_TX_DISCARD_QUANTUM < frms) {
            frms = OL_TX_DISCARD_QUANTUM;
        }

        if (txq->frms > 1 && frms >= (txq->frms >> 1)) {
            frms = txq->frms >> 1;
        }
    }

    /*
     * Discard from the head of the queue, because:
     * 1.  Front-dropping gives applications like TCP that include ARQ
     *     an early notification of congestion.
     * 2.  For time-sensitive applications like RTP, the newest frames are
     *     most relevant.
     */
    credit = 10000; /* no credit limit */
    frms = ol_tx_dequeue(pdev, txq, tx_descs, frms, &credit, &bytes);

    notify_ctx.event = OL_TX_DISCARD_FRAMES;
    notify_ctx.frames = frms;
    notify_ctx.bytes = bytes;
    notify_ctx.txq = txq;
    notify_ctx.info.ext_tid = cat;
    ol_tx_sched_notify(pdev, &notify_ctx);

    TX_SCHED_DEBUG_PRINT("%s Tx Drop : %d\n",__func__,frms);
    return frms;
}

/*--- scheduler framework ---------------------------------------------------*/

/*
 * The scheduler mutex spinlock has been acquired outside this function,
 * so there is need to take locks inside this function.
 */
void
ol_tx_sched_notify(
    struct ol_txrx_pdev_t *pdev,
    struct ol_tx_sched_notify_ctx_t *ctx)
{
    struct ol_tx_frms_queue_t *txq = ctx->txq;
    int tid;

    if (!pdev->tx_sched.scheduler) {
        return;
    }

    switch (ctx->event) {
    case OL_TX_ENQUEUE_FRAME:
        tid = ctx->info.tx_msdu_info->htt.info.ext_tid;
        ol_tx_sched_txq_enqueue(pdev, txq, tid, 1, ctx->bytes);
        break;
    case OL_TX_DELETE_QUEUE:
        tid = ctx->info.ext_tid;
        if (txq->flag == ol_tx_queue_active) {
            ol_tx_sched_txq_deactivate(pdev, txq, tid);
        }
        break;
    case OL_TX_PAUSE_QUEUE:
        tid = ctx->info.ext_tid;
        if (txq->flag == ol_tx_queue_active) {
            ol_tx_sched_txq_deactivate(pdev, txq, tid);
        }
        break;
    case OL_TX_UNPAUSE_QUEUE:
        tid = ctx->info.ext_tid;
        if (txq->frms != 0) {
            ol_tx_sched_txq_enqueue(pdev, txq, tid, txq->frms, txq->bytes);
        }
        break;
    case OL_TX_DISCARD_FRAMES:
        tid = ctx->info.ext_tid; /* not necessarily TID, could be category */
        ol_tx_sched_txq_discard(pdev, txq, tid, ctx->frames, ctx->bytes);
        break;
    default:
        VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
            "Error: unknown sched notification (%d)\n", ctx->event);
        adf_os_assert(0);
        break;
    }
}

#define OL_TX_MSDU_ID_STORAGE_ERR(ptr) (NULL == ptr)

void
ol_tx_sched_dispatch(
  struct ol_txrx_pdev_t *pdev,
  struct ol_tx_sched_ctx *sctx)
{
    adf_nbuf_t msdu, prev = NULL, head_msdu = NULL;
    struct ol_tx_desc_t *tx_desc;

    u_int16_t *msdu_id_storage;
    u_int16_t msdu_id;
    int num_msdus = 0;
    TX_SCHED_DEBUG_PRINT("Enter %s\n", __func__);
    while (sctx->frms)
    {
        tx_desc = TAILQ_FIRST(&sctx->head);
        if (tx_desc == NULL){
            /* TODO: find its reason */
            VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_ERROR,
                "%s: err, no enough tx_desc from stx->head.\n", __func__);
            break;
        }
        msdu = tx_desc->netbuf;
        TAILQ_REMOVE(&sctx->head, tx_desc, tx_desc_list_elem);
        if (NULL == head_msdu)
        {
            head_msdu = msdu;
        }
        if (prev)
        {
             adf_nbuf_set_next(prev, msdu);
        }
        prev = msdu;

#ifndef ATH_11AC_TXCOMPACT
        /*
         * When the tx frame is downloaded to the target, there are two
         * outstanding references:
         * 1.  The host download SW (HTT, HTC, HIF)
         *     This reference is cleared by the ol_tx_send_done callback
         *     functions.
         * 2.  The target FW
         *     This reference is cleared by the ol_tx_completion_handler
         *     function.
         * It is extremely probable that the download completion is processed
         * before the tx completion message.  However, under exceptional
         * conditions the tx completion may be processed first.  Thus, rather
         * that assuming that reference (1) is done before reference (2),
         * explicit reference tracking is needed.
         * Double-increment the ref count to account for both references
         * described above.
         */
        adf_os_atomic_init(&tx_desc->ref_cnt);
        adf_os_atomic_inc(&tx_desc->ref_cnt);
        adf_os_atomic_inc(&tx_desc->ref_cnt);
#endif

        //Store the MSDU Id for each MSDU
        /* store MSDU ID */
        msdu_id = ol_tx_desc_id(pdev, tx_desc);
        msdu_id_storage = ol_tx_msdu_id_storage(msdu);
        if (OL_TX_MSDU_ID_STORAGE_ERR(msdu_id_storage)) {
            /*
             * Send the prior frames as a batch, then send this as a single,
             * then resume handling the remaining frames.
             */
            if (head_msdu){
                ol_tx_send_batch(pdev, head_msdu, num_msdus);
            }
            head_msdu = prev = NULL;
            num_msdus = 0;

            if (htt_tx_send_std(pdev->htt_pdev, msdu, msdu_id)) {
                OL_TX_TARGET_CREDIT_INCR(pdev, msdu);
                ol_tx_desc_frame_free_nonstd(pdev, tx_desc, 1 /* error */);
            }
        } else {
            *msdu_id_storage = msdu_id;
            num_msdus++;
        }
        sctx->frms--;
    }

    //Send Batch Of Frames
    if (head_msdu)
    {
        ol_tx_send_batch(pdev,head_msdu,num_msdus);
    }
    TX_SCHED_DEBUG_PRINT("Leave %s\n", __func__);
}

void
ol_tx_sched(struct ol_txrx_pdev_t *pdev)
{
    struct ol_tx_sched_ctx sctx;
    u_int32_t credit;

    TX_SCHED_DEBUG_PRINT("Enter %s\n", __func__);
    adf_os_spin_lock_bh(&pdev->tx_queue_spinlock);
    if (pdev->tx_sched.tx_sched_status != ol_tx_scheduler_idle) {
        adf_os_spin_unlock_bh(&pdev->tx_queue_spinlock);
        return;
    }
    pdev->tx_sched.tx_sched_status = ol_tx_scheduler_running;

    OL_TX_SCHED_LOG(pdev);
    //adf_os_print("BEFORE tx sched:\n");
    //ol_tx_queues_display(pdev);
    adf_os_spin_unlock_bh(&pdev->tx_queue_spinlock);

    TAILQ_INIT(&sctx.head);
    sctx.frms = 0;

    ol_tx_sched_select_init(pdev);
    while (adf_os_atomic_read(&pdev->target_tx_credit) > 0) {
        int num_credits;
        adf_os_spin_lock_bh(&pdev->tx_queue_spinlock);
        credit = adf_os_atomic_read(&pdev->target_tx_credit);
        num_credits = ol_tx_sched_select_batch(pdev, &sctx, credit);
        if (num_credits > 0){
#if DEBUG_HTT_CREDIT
            VOS_TRACE(VOS_MODULE_ID_TXRX, VOS_TRACE_LEVEL_INFO,
                " <HTT> Decrease credit %d - %d = %d.\n",
                adf_os_atomic_read(&pdev->target_tx_credit),
                num_credits,
                adf_os_atomic_read(&pdev->target_tx_credit) - num_credits);
#endif
            adf_os_atomic_add(-num_credits, &pdev->target_tx_credit);
        }
        adf_os_spin_unlock_bh(&pdev->tx_queue_spinlock);

        if (num_credits == 0) break;
    }
    ol_tx_sched_dispatch(pdev, &sctx);

    adf_os_spin_lock_bh(&pdev->tx_queue_spinlock);
    //adf_os_print("AFTER tx sched:\n");
    //ol_tx_queues_display(pdev);

    pdev->tx_sched.tx_sched_status = ol_tx_scheduler_idle;
    adf_os_spin_unlock_bh(&pdev->tx_queue_spinlock);
    TX_SCHED_DEBUG_PRINT("Leave %s\n", __func__);
}

void *
ol_tx_sched_attach(
  struct ol_txrx_pdev_t *pdev)
{
    pdev->tx_sched.tx_sched_status = ol_tx_scheduler_idle;
    return ol_tx_sched_init(pdev);
}

void
ol_tx_sched_detach(
  struct ol_txrx_pdev_t *pdev)
{
    if (pdev->tx_sched.scheduler) {
        adf_os_mem_free(pdev->tx_sched.scheduler);
        pdev->tx_sched.scheduler = NULL;
    }
}

/*--- debug functions -------------------------------------------------------*/

#if defined(DEBUG_HL_LOGGING)

static void
ol_tx_sched_log(struct ol_txrx_pdev_t *pdev)
{
    u_int8_t  *buf;
    u_int32_t *active_bitmap;
    int i, j, num_cats_active;
    int active, frms, bytes;
    int credit;

    /* don't bother recording state if credit is zero */
    credit = adf_os_atomic_read(&pdev->target_tx_credit);
    if (credit == 0) {
        return;
    }

    /*
     * See how many TIDs are active, so queue state can be stored only
     * for those TIDs.
     * Do an initial iteration through all categories to see if any
     * are active.  Doing an extra iteration is inefficient, but
     * efficiency is not a dominant concern when logging is enabled.
     */
    num_cats_active = 0;
    for (i = 0; i < OL_TX_SCHED_NUM_CATEGORIES; i++) {
        ol_tx_sched_category_info(pdev, i, &active, &frms, &bytes);
        if (active) {
            num_cats_active++;
        }
    }
    /* don't bother recording state if there are no active queues */
    if (num_cats_active == 0) {
        return;
    }

    OL_TX_QUEUE_LOG_SCHED(pdev, credit, &num_cats_active, &active_bitmap, &buf);

    if (num_cats_active == 0) {
        return;
    }
    *active_bitmap = 0;
    for (i = 0, j = 0;
         i < OL_TX_SCHED_NUM_CATEGORIES && j < num_cats_active;
         i++)
    {
        u_int8_t *p;
        ol_tx_sched_category_info(pdev, i, &active, &frms, &bytes);
        if (!active) {
            continue;
        }
        p = &buf[j*6];
        p[0]   = (frms >> 0) & 0xff;
        p[1] = (frms >> 8) & 0xff;

        p[2] = (bytes >> 0) & 0xff;
        p[3] = (bytes >> 8) & 0xff;
        p[4] = (bytes >> 16) & 0xff;
        p[5] = (bytes >> 24) & 0xff;
        j++;
        *active_bitmap |= 1 << i;
    }
}

#endif /* defined(DEBUG_HL_LOGGING) */

#endif /* defined(CONFIG_HL_SUPPORT) */
