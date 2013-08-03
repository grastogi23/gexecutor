/**
 * gtaskq.h
 *  Copyright Admin Cppexecutor, 2013
 *  Created on: Jun 23, 2013
 *      Author: cppexecutor@gmail.com
 */

#ifndef GTASKQ_H_
#define GTASKQ_H_
#include "gexecutor_common.h"
#include "gexecutor.h"
#include <queue>
#include <deque>
#include <sys/wait.h>
#include <unistd.h>
#include <glog/logging.h>
#include <assert.h>
#include <pthread.h>
class GTask;
class GExecutor;
class GTaskQ;


/**
 * This is the task interface
 *
 */
class GTask {
public:
    /**
     * Need to initialize the GTask where the response should be sent.
     *
     * @param response_queue_: This is the queue for sending back response
     * for this task
     */
    explicit GTask(GTaskQ* response_queue_);
    virtual ~GTask();

    /**
     * If done response is required then the GTask would be sent back to the
     * executor which originated this task
     *
     * GTask *p_task;
     * p_task->execute();
     * if (p_task->IsDoneResponseRequired()) {
     *      originator_executor->EnQueueTaskResponse(this);
     * }
     *
     * @return
     */
    virtual gerror_code_t Execute() {
        GEXECUTOR_LOG(GEXECUTOR_TRACE) <<
                "Default Task Called\n";
        return 0;
    }

    void set_exec_task_q(GTaskQ* execution_task_q) {
        assert(exec_task_q_ == NULL);
        exec_task_q_ = execution_task_q;
    }

    void set_executor(GExecutor *executor) {
        executor_ = executor;
    }
protected:
    GTaskQ* resp_task_q_;
    GTaskQ* exec_task_q_;
    GExecutor* executor_;
};


/**
 *
 *
 *
 */

class GTaskQ {
public:
    GTaskQ();
    virtual ~GTaskQ();
    /**
     *
     * @param task
     * @param ev_base: This is event base of the caller. It is possible that
     * equeue may need to be buffered as signal to the other end has not
     * happened or need to be re-tried.
     * @return
     */
    virtual gerror_code_t Initialize();
    virtual gerror_code_t EnqueueGTask(GTask *task,
                                       GExecutor* executor_ctx = NULL);
    virtual GTask* DequeueGTask();
    virtual int read_fd() {
        return pipefds_[PIPE_FD_READ_INDX];
    }
    virtual int write_fd() {
        return pipefds_[PIPE_FD_WRITE_INDX];
    }
    virtual void set_gexecutor(GExecutor* executor_ctx) {
        executor_ctx_ = executor_ctx;
    }
    int64_t num_enqueue() {
        return num_enqueue_;
    }

    int64_t num_dequeue() {
        return num_dequeue_;
    }

protected:
    virtual gerror_code_t NotifyQ(size_t num_notifn,
                                  ssize_t *num_notified);
    size_t num_notifn_to_write(size_t num_new_notifn);
    int64_t num_enqueue_;
    int64_t num_dequeue_;
private:
    size_t max_outstanding_notifn_;
    enum PIPE_FD_INDX {
        PIPE_FD_READ_INDX = 0,
        PIPE_FD_WRITE_INDX = 1,
        PIPE_NUM_FDS = PIPE_FD_WRITE_INDX
    };
    std::deque<GTask *> task_queue_;
    GExecutor* executor_ctx_;
    size_t num_outstanding_notifn_;
    int pipefds_[PIPE_NUM_FDS];
    char *notfn_buffer_;
    pthread_mutex_t q_lock_;
};

#endif /* GTASKQ_H_ */