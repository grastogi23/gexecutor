/**
 * gexecutor_unittest.cc
 *  Copyright Admin Cppexecutor, 2013
 *  Created on: Jun 9, 2013
 *      Author: cppexecutor@gmail.com
 */

#include <gtest/gtest.h>
#include <iostream>
#include <glog/logging.h>
#include <gexecutor.h>
#include <gasync_executor.h>
#include <gsync_executor.h>
#include <set>
#include <event2/event.h>
#include <pthread.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <gflags/gflags.h>


using namespace std;
DEFINE_int64(timeout_us, 100000, "timeout value in usecs for events");
DEFINE_int64(timeout_s, 0, "timeout value in secs");
DEFINE_int32(num_threads, 2, "Number of threads for running tests");
DEFINE_int32(max_events, 3, "Max Number of events for tests");

class GExecutorTest : public testing::Test {
public:
    void CheckForEventExecution(std::set<bool *> events_list) {
        return;
    }

protected:
    struct event_base *event_base_;
    virtual void SetUp() {
        event_base_ = event_base_new();
        if (!event_base_) {
            ASSERT_TRUE(event_base_ != NULL);
            GEXECUTOR_LOG(GEXECUTOR_TRACE) << "Event Base null\n";
            return;
        }

    }
    virtual void TearDown() {
        if (event_base_) {
            free(event_base_);
        }
    }

};




TEST_F(GExecutorTest, SampleSmoke) {
  // You can access data in the test fixture here.
    ASSERT_TRUE(1);
}

/**
 *
 */

class GTaskHello : public GTask {
public:
    GTaskHello(GTaskQ* taskq)
    : GTask(taskq), id_(0) {
    }
    virtual ~GTaskHello() {
        return;
    }
    void set_id(int id) {
           id_ = id;
    }
protected:
    virtual gerror_code_t Execute() {
        GEXECUTOR_LOG(GEXECUTOR_TRACE) << "Hello Task ID" << id_ << std::endl;
        delete this;
        return 0;
    }
private:
    int id_;
};


class GTaskPing : public GTask {
public:
    GTaskPing(GTaskQ* resp_task_q, const std::string& msg)
        : GTask(resp_task_q), id_(0), msg_(msg) {
    }
    virtual ~GTaskPing() {
        return;
    }
    void set_id(int id) {
           id_ = id;
    }
    const std::string& DebugString() {
        return msg_;
    }
protected:
    virtual gerror_code_t Execute() {
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
                << "Received Hello Task ID: "
                << id_ << " : " << DebugString()
                << " respq: " << resp_task_q_
                << " --> execq: " << exec_task_q_
                << std::endl;
        if (id_%100 == 0) {
            LOG(ERROR) << "Hello Task ID:" << id_ << " msg: " << DebugString()
                    << std::endl;
        }

        if (exec_task_q_->num_dequeue() >=
                FLAGS_max_events*(FLAGS_num_threads-1)) {
            /**
             * stop the thread
             */
            GEXECUTOR_LOG(GEXECUTOR_TRACE)
                 << " Shutdown of queue: " << exec_task_q_
                 << " NumDeQ: " << exec_task_q_->num_dequeue()
                 << std::endl;
            executor_->Shutdown();
        }

        if (id_ < (2*FLAGS_max_events -1)) {
            std::string new_msg = "ping";
            if (msg_ == "ping") {
                /**
                 * send pong
                 */
                new_msg = "pong";
            }

            GEXECUTOR_LOG(GEXECUTOR_TRACE)
                    << "Sending Hello Task ID: "
                    << id_+1 << " : " << new_msg
                    << " execq: " << exec_task_q_
                    << " --> respq: " << resp_task_q_
                    << std::endl;

            GTaskPing *p_resp_task =
                    new GTaskPing(exec_task_q_, new_msg);
            p_resp_task->set_id(id_ + 1);
            resp_task_q_->EnqueueGTask(p_resp_task);
        }
        delete this;
        return 0;
    }
private:
    int id_;
    std::string msg_;
};

class GTaskCheckTaskQ : public GTask {
public:
    GTaskCheckTaskQ(GTaskQ* taskq)
    : GTask(taskq), id_(0) {
    }
    virtual ~GTaskCheckTaskQ() {
        return;
    }
    void set_id(int id) {
        id_ = id;
    }
protected:
    virtual gerror_code_t Execute() {
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
                << "Hello Task SRC ID" << id_ << std::endl;
        delete this;
        return 0;
    }
private:
    int id_;
};


TEST_F(GExecutorTest, InitializeConstructorsSmoke) {
    GTaskQ* taskq = new GTaskQ();

    GExecutor *async_engine =
            new GAsyncExecutor(GExecutor::ASYNC,
                               event_base_, taskq);

    GExecutor *sync_engine =
            new GSyncExecutor(GExecutor::SYNC, taskq);

    ASSERT_TRUE(async_engine != NULL);
    ASSERT_TRUE(sync_engine != NULL);

    delete async_engine;
    delete sync_engine;
    delete taskq;
}


class ThreadInfo {
public:
    enum GExecutorTestType {
        HELLO=0,
        PINGPONG=1
    };

    pthread_t thread_id;        /* ID returned by pthread_create() */
    int       thread_num;       /* Application-defined thread # */
    int       max_events;
    GTaskQ*   taskq;
    static    int num_queues;
    struct event *timer_ev;
    struct event_base* async_base;
    void (*start_routine)(evutil_socket_t fd, short what, void *arg);
    GExecutorTestType task_type;
    static std::vector<GExecutor *>& executor_list() {
        if (executor_list_ == NULL) {
            executor_list_ =
                    new std::vector<GExecutor*>(FLAGS_num_threads);
        }
        return *executor_list_;
    }
    bool is_thread_stopped;
    bool ping_started;

private:
    static    std::vector<GExecutor*>* executor_list_;
};

int ThreadInfo::num_queues = FLAGS_num_threads;
std::vector<GExecutor*>* ThreadInfo::executor_list_ = 0;

static void timer_cb_func(evutil_socket_t fd, short what, void *arg) {
    ThreadInfo* p_thread_info =
            static_cast<ThreadInfo *>(arg);

    if (!p_thread_info) {
        ASSERT_TRUE(p_thread_info);
        return;
    }

    GTaskQ *p_resp_taskq =
            ThreadInfo::executor_list()[p_thread_info->thread_num]->taskq();

    GEXECUTOR_LOG(GEXECUTOR_TRACE)
        << "Thread: " << p_thread_info->thread_num << " "
        << __FUNCTION__
        << " numEnQ: " << p_resp_taskq->num_enqueue()
        << " numDeQ: " << p_resp_taskq->num_dequeue()
        << "\n";

    for (int thread_indx = 0;
            ((thread_indx < FLAGS_num_threads) &&
                    !p_thread_info->ping_started);
            thread_indx++) {
        if ((thread_indx == p_thread_info->thread_num) ||
                (ThreadInfo::executor_list()[thread_indx] == NULL)) {
            /**
             * ignore this queue.
             */
            continue;
        }

        GExecutor* dest_engine =
                ThreadInfo::executor_list()[thread_indx];
        GTaskQ *p_destq = dest_engine->taskq();

        if ((p_thread_info->task_type == ThreadInfo::HELLO) &&
            (p_destq->num_enqueue() >=
                    p_thread_info->max_events*(ThreadInfo::num_queues-1))) {
            continue;
        }

        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " Thread: " << p_thread_info->thread_num
            << " Destn taskq:" << thread_indx
            << " NumDestEnQ: " << p_destq->num_enqueue()
            << " NumDestDeQ: " << p_destq->num_dequeue()
            << " NumRespEnQ: " << p_resp_taskq->num_enqueue()
            << " NumRespDeQ: " << p_resp_taskq->num_dequeue()
            << " MaxEvents: " << p_thread_info->max_events
            << std::endl;

        GTask* p_task = NULL;
        if (p_thread_info->task_type == ThreadInfo::HELLO) {
            GTaskHello *task_ = new GTaskHello(p_resp_taskq);
            task_->set_id(p_thread_info->thread_num);
            p_task = task_;
            p_destq->EnqueueGTask(p_task, NULL);
        } else if (p_thread_info->task_type == ThreadInfo::PINGPONG) {
            GTaskPing *task_ =
                    new GTaskPing(p_resp_taskq, "ping");
            task_->set_id(p_thread_info->thread_num);
            p_task = task_;
            p_destq->EnqueueGTask(p_task, NULL);

        }
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " Dest Thread: " << thread_indx
            << " Num tasks enqueued " << p_destq->num_enqueue()
            << " Num tasks dequeued " << p_destq->num_dequeue()
            << std::endl;
    }

    if (p_thread_info->task_type == ThreadInfo::PINGPONG) {
        p_thread_info->ping_started = true;
    }

    int64_t num_dequeue_per_thread =
            p_thread_info->max_events*(ThreadInfo::num_queues-1);

    if (p_resp_taskq->num_dequeue() >= num_dequeue_per_thread) {
        /**
         * stop the engine as it has processed all events.
         */
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
                    <<"Shutdown of thread: " << p_thread_info->thread_num
                    <<" Num DeQ: " << p_resp_taskq->num_dequeue()
                    << std::endl;
        event_free(p_thread_info->timer_ev);
        gerror_code_t rc =
            ThreadInfo::executor_list()[p_thread_info->thread_num]->Shutdown();
        p_thread_info->is_thread_stopped = true;
        ThreadInfo::executor_list()[p_thread_info->thread_num] = NULL;
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            <<"Engine shutdown: "<< p_thread_info->thread_num
            << std::endl;
        ASSERT_EQ(rc, 0);
        return;
    }

    // event_free(p_thread_info->timer_ev);
    p_thread_info->timer_ev =
            event_new(p_thread_info->async_base, -1, EV_TIMEOUT,
                      timer_cb_func, p_thread_info);
    struct timeval timeout = { FLAGS_timeout_s, FLAGS_timeout_us };
    event_add(p_thread_info->timer_ev, &timeout);
    return;
}


static void ping_pong_timer_cb_func(evutil_socket_t fd, short what, void *arg) {
    ThreadInfo* p_thread_info =
            static_cast<ThreadInfo *>(arg);

    if (!p_thread_info) {
        ASSERT_TRUE(p_thread_info);
        return;
    }

    GTaskQ *p_resp_taskq =
            ThreadInfo::executor_list()[p_thread_info->thread_num]->taskq();

    GEXECUTOR_LOG(GEXECUTOR_TRACE)
        << "Thread: " << p_thread_info->thread_num << " "
        << __FUNCTION__
        << " numEnQ: " << p_resp_taskq->num_enqueue()
        << " numDeQ: " << p_resp_taskq->num_dequeue()
        << "\n";

    for (int thread_indx = 0;
            ((thread_indx < FLAGS_num_threads));
            thread_indx++) {
        if ((thread_indx == p_thread_info->thread_num) ||
                (ThreadInfo::executor_list()[thread_indx] == NULL)) {
            /**
             * ignore this queue.
             */
            continue;
        }

        GExecutor* dest_engine =
                ThreadInfo::executor_list()[thread_indx];
        GTaskQ *p_destq = dest_engine->taskq();

        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " Thread: " << p_thread_info->thread_num
            << " --> Destn taskq:" << thread_indx
            << " NumDestEnQ: " << p_destq->num_enqueue()
            << " NumDestDeQ: " << p_destq->num_dequeue()
            << " NumRespEnQ: " << p_resp_taskq->num_enqueue()
            << " NumRespDeQ: " << p_resp_taskq->num_dequeue()
            << " MaxEvents: " << p_thread_info->max_events
            << std::endl;

        GTaskPing *task_ =
                new GTaskPing(p_resp_taskq, "ping");
        p_destq->EnqueueGTask(task_, NULL);
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " Dest Thread: " << thread_indx
            << " Num tasks enqueued " << p_destq->num_enqueue()
            << " Num tasks dequeued " << p_destq->num_dequeue()
            << std::endl;
    }

    if (p_thread_info->ping_started) {
        assert(0);
    }
    p_thread_info->ping_started = true;

    //event_free(p_thread_info->timer_ev);
    //p_thread_info->timer_ev = NULL;
//    // event_free(p_thread_info->timer_ev);
//    p_thread_info->timer_ev =
//            event_new(p_thread_info->async_base, -1, EV_TIMEOUT,
//                      ping_pong_timer_cb_func, p_thread_info);
//    struct timeval timeout = { FLAGS_timeout_s, FLAGS_timeout_us };
//    event_add(p_thread_info->timer_ev, &timeout);
    return;
}


static void *gasync_executor_thread(void *args) {

    ThreadInfo* p_thread_info =
            static_cast<ThreadInfo*>(args);

    struct event_base* async_base = event_base_new();
    GAsyncExecutor *async_engine =
            new GAsyncExecutor(GExecutor::ASYNC,
                               async_base,
                               p_thread_info->taskq);

    ThreadInfo::executor_list()[p_thread_info->thread_num] =
            async_engine;

    GEXECUTOR_LOG(GEXECUTOR_TRACE)
           << "Starting Worker Thread " << p_thread_info->thread_num
           << " thread id " << p_thread_info->thread_id
           << " Async Engine " << std::hex << async_engine << std::endl;

    async_engine->Initialize();

    /**
     * Goal is for this timer event to add a task for other async engines.
     */

    GEXECUTOR_LOG(GEXECUTOR_TRACE)
               << "Adding timer thread " << p_thread_info->thread_num
               << " thread id " << p_thread_info->thread_id << std::endl;
    struct timeval timeout = { FLAGS_timeout_s, FLAGS_timeout_us };
    struct event *ev;
    void *timer_arg = static_cast<void *>(p_thread_info);
    ev = event_new(async_base, -1, EV_TIMEOUT,
                   *(p_thread_info->start_routine), timer_arg);
    event_add(ev, &timeout);
    p_thread_info->async_base = async_base;
    event_base_dispatch(async_base);
    delete async_engine;
    ThreadInfo::executor_list()[p_thread_info->thread_num] = NULL;
    return p_thread_info;
}


TEST_F(GExecutorTest, InitializeAsyncSmoke) {
    pthread_attr_t attr1;
    ThreadInfo *tinfo = NULL;
    int rc = 0;


    rc = pthread_attr_init(&attr1);
    ASSERT_EQ(rc, 0);

    pthread_attr_setstacksize(&attr1, 1024*1024);

    tinfo = static_cast<ThreadInfo *>(
            calloc(FLAGS_num_threads, sizeof(ThreadInfo)));

    ASSERT_TRUE(tinfo != NULL);

    std::vector<GExecutor *> executor_list(FLAGS_num_threads);
    executor_list[0] = NULL;
    executor_list[1] = NULL;

    /* Create one thread for each command-line argument */

    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        tinfo[tnum].thread_num = tnum;
        tinfo[tnum].max_events = FLAGS_max_events;
        tinfo[tnum].taskq = new GTaskQ();
        rc = tinfo[tnum].taskq->Initialize();
        tinfo[tnum].start_routine = timer_cb_func;
        tinfo[tnum].task_type = ThreadInfo::HELLO;
        ASSERT_EQ(rc, 0);
        /* The pthread_create() call stores the thread ID into
                     corresponding element of tinfo[] */

        rc = pthread_create(&tinfo[tnum].thread_id, &attr1,
                           &gasync_executor_thread, &tinfo[tnum]);
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << "Created thread with id" << tinfo[tnum].thread_id << std::endl;
        ASSERT_EQ(rc, 0);
    }

    rc = pthread_attr_destroy(&attr1);
    ASSERT_EQ(rc, 0);

    /* Now join with each thread, and display its returned value */
    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        void *res = NULL;
        rc = pthread_join(tinfo[tnum].thread_id, &res);
        ASSERT_EQ(rc, 0);

        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            <<  "Joined with thread "<< tinfo[tnum].thread_num
            <<  "returned value was " << res << std::endl;
        //free(res);      /* Free memory allocated by thread */
    }
    free(tinfo);
}



TEST_F(GExecutorTest, InitializeAsyncPingPongSmoke) {
    pthread_attr_t attr1;
    ThreadInfo *tinfo = NULL;
    int rc = 0;


    rc = pthread_attr_init(&attr1);
    ASSERT_EQ(rc, 0);

    pthread_attr_setstacksize(&attr1, 1024*1024);

    tinfo = static_cast<ThreadInfo *>(
            calloc(FLAGS_num_threads, sizeof(ThreadInfo)));

    ASSERT_TRUE(tinfo != NULL);

    std::vector<GExecutor *> executor_list(FLAGS_num_threads);

    /* Create one thread for each command-line argument */

    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        tinfo[tnum].thread_num = tnum;
        tinfo[tnum].max_events = FLAGS_max_events;
        tinfo[tnum].taskq = new GTaskQ();
        tinfo[tnum].start_routine = ping_pong_timer_cb_func;
        tinfo[tnum].task_type = ThreadInfo::PINGPONG;
        rc = tinfo[tnum].taskq->Initialize();
        ASSERT_EQ(rc, 0);
        /* The pthread_create() call stores the thread ID into
                     corresponding element of tinfo[] */

        rc = pthread_create(&tinfo[tnum].thread_id, &attr1,
                           &gasync_executor_thread, &tinfo[tnum]);
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << "Created thread with id" << tinfo[tnum].thread_id << std::endl;
        ASSERT_EQ(rc, 0);
    }

    rc = pthread_attr_destroy(&attr1);
    ASSERT_EQ(rc, 0);

    /* Now join with each thread, and display its returned value */
    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        void *res = NULL;
        rc = pthread_join(tinfo[tnum].thread_id, &res);
        ASSERT_EQ(rc, 0);

        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            <<  "Joined with thread "<< tinfo[tnum].thread_num
            <<  "returned value was " << res << std::endl;
        //free(res);      /* Free memory allocated by thread */
    }
    free(tinfo);
}



TEST_F(GExecutorTest, InitializeSyncSmoke) {
    pthread_attr_t attr1;
    ThreadInfo *tinfo = NULL;
    int rc = 0;


    rc = pthread_attr_init(&attr1);
    ASSERT_EQ(rc, 0);

    pthread_attr_setstacksize(&attr1, 1024*1024);

    tinfo = static_cast<ThreadInfo *>(
            calloc(FLAGS_num_threads, sizeof(ThreadInfo)));

    ASSERT_TRUE(tinfo != NULL);

    std::vector<GExecutor *> executor_list(FLAGS_num_threads);
    executor_list[0] = NULL;
    executor_list[1] = NULL;

    /* Create one thread for each command-line argument */

    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        tinfo[tnum].thread_num = tnum;
        tinfo[tnum].max_events = FLAGS_max_events;
        tinfo[tnum].taskq = new GTaskQ();
        rc = tinfo[tnum].taskq->Initialize();
        tinfo[tnum].start_routine = timer_cb_func;
        tinfo[tnum].task_type = ThreadInfo::HELLO;
        ASSERT_EQ(rc, 0);
        /* The pthread_create() call stores the thread ID into
                     corresponding element of tinfo[] */

        rc = pthread_create(&tinfo[tnum].thread_id, &attr1,
                           &gasync_executor_thread, &tinfo[tnum]);
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << "Created thread with id" << tinfo[tnum].thread_id << std::endl;
        ASSERT_EQ(rc, 0);
    }

    rc = pthread_attr_destroy(&attr1);
    ASSERT_EQ(rc, 0);

    /* Now join with each thread, and display its returned value */
    for (int tnum = 0; tnum < FLAGS_num_threads; tnum++) {
        void *res = NULL;
        rc = pthread_join(tinfo[tnum].thread_id, &res);
        ASSERT_EQ(rc, 0);

        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            <<  "Joined with thread "<< tinfo[tnum].thread_num
            <<  "returned value was " << res << std::endl;
        //free(res);      /* Free memory allocated by thread */
    }
    free(tinfo);
}



class GeTestEnvironment: public testing::Environment {
public:
    virtual void SetUp() {
        ThreadInfo::num_queues = FLAGS_num_threads;
    }

    virtual void TearDown() {
    }
};

int main(int argc, char *argv[]) {
    int result;

    google::InitGoogleLogging(argv[0]);

    FLAGS_logtostderr = 0;

    testing::InitGoogleTest(&argc, argv);

    char curr_dir[1024];
    getcwd(curr_dir, sizeof(curr_dir));
    cout << "current dir" << curr_dir << endl;
    google::ParseCommandLineFlags(&argc, &argv, true);
    FLAGS_logbufsecs = 0;
    testing::AddGlobalTestEnvironment(new GeTestEnvironment());

    result = RUN_ALL_TESTS();

    return result;
}






class GSyncExecutorTest : public testing::Test {
public:

protected:
    struct event_base *event_base_;
    virtual void SetUp() {
        event_base_ = event_base_new();
        if (!event_base_) {
            ASSERT_TRUE(event_base_ != NULL);
            GEXECUTOR_LOG(GEXECUTOR_TRACE) << "Event Base null\n";
            return;
        }

    }
    virtual void TearDown() {
        if (event_base_) {
            event_base_free(event_base_);
        }
    }
};


class HybridApp {
public:
    GExecutor* sync;
    GExecutor* async;
    struct event *timer_ev;
    struct event_base *async_base;
};



static void hybrid_timer_cb(evutil_socket_t fd, short what, void *arg) {
    HybridApp* p_app =
            static_cast<HybridApp *>(arg);

    if (!p_app) {
        ASSERT_TRUE(p_app);
        return;
    }

    GTaskQ *p_resp_taskq = p_app->async->taskq();

    GEXECUTOR_LOG(GEXECUTOR_TRACE)
        << "Async: "
        << __FUNCTION__
        << " numEnQ: " << p_resp_taskq->num_enqueue()
        << " numDeQ: " << p_resp_taskq->num_dequeue()
        << "\n";

    GTaskQ *p_destq = p_app->sync->taskq();

    if (p_destq->num_enqueue() < FLAGS_max_events) {
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " NumDestEnQ: " << p_destq->num_enqueue()
            << " NumDestDeQ: " << p_destq->num_dequeue()
            << " NumRespEnQ: " << p_resp_taskq->num_enqueue()
            << " NumRespDeQ: " << p_resp_taskq->num_dequeue()
            << " MaxEvents: " << FLAGS_max_events
            << std::endl;

        GTask* p_task = new GTaskHello(p_resp_taskq);
        p_destq->EnqueueGTask(p_task);
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << " Num tasks enqueued " << p_destq->num_enqueue()
            << " Num tasks dequeued " << p_destq->num_dequeue()
            << std::endl;
    } else {
        p_app->sync->Shutdown();
        /**
         * stop the engine as it has processed all events.
         */
        GEXECUTOR_LOG(GEXECUTOR_TRACE)
                    <<"Shutdown of async thread"
                    <<" Num DeQ: " << p_resp_taskq->num_dequeue()
                    << std::endl;
        p_app->async->Shutdown();
        return;
    }

    // event_free(p_thread_info->timer_ev);
    p_app->timer_ev =
            event_new(p_app->async_base, -1, EV_TIMEOUT,
                      hybrid_timer_cb, p_app);
    struct timeval timeout = { FLAGS_timeout_s, FLAGS_timeout_us };
    event_add(p_app->timer_ev, &timeout);
    return;
}




TEST_F(GSyncExecutorTest, SyncWorkerSmoke) {
    GTaskQ* sync_taskq = new GTaskQ();
    sync_taskq->Initialize();
    GTaskQ* async_taskq = new GTaskQ();
    async_taskq->Initialize();
    GSyncExecutor *sync_engine =
            new GSyncExecutor(GExecutor::SYNC, sync_taskq);

    //this woudl start the workers.
    sync_engine->Initialize();

    GAsyncExecutor* async_engine =
            new GAsyncExecutor(GExecutor::ASYNC,
                               event_base_, async_taskq);

    async_engine->Initialize();


    HybridApp happ;
    happ.async = async_engine;
    happ.sync = sync_engine;
    happ.async_base = event_base_;

    struct timeval timeout = { FLAGS_timeout_s, FLAGS_timeout_us };
    void *timer_arg = static_cast<void *>(&happ);
    happ.timer_ev = event_new(event_base_,
                        -1,
                        EV_TIMEOUT,
                        hybrid_timer_cb,
                        timer_arg);
    event_add(happ.timer_ev, &timeout);
    event_base_dispatch(event_base_);
    event_free(happ.timer_ev);
    delete async_engine;
    delete sync_engine;
    return;
}






