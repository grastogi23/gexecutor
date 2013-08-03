/**
 * gexecutor.cc
 *  Copyright Admin Cppexecutor, 2013
 *  Created on: Jun 9, 2013
 *      Author: cppexecutor@gmail.com
 */

#include "gexecutor.h"




GExecutor::GExecutor(GExecutorType type,
                     //struct event_base *event_base,
                     GTaskQ* taskq)
    : //event_base_(event_base),
      gexec_type_(type),
      num_enqueue_(0), num_dequeue_(0), num_task_with_response_(0),
      rate_of_dequeue_in_100secs(0), max_rate_of_dequeue(0),
      rate_of_enqueue_in_100secs(0), max_rate_of_enqueue(0) {
}

GExecutor::~GExecutor() {

}

void GExecutor::PrintTo(const GExecutor& executor, ::std::ostrstream* os) {
    *os << "Gexecutor: type["<< executor.type() << "]" << std::endl;
}