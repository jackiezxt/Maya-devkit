/*
    Copyright 2005-2014 Intel Corporation.  All Rights Reserved.

    The source code contained or described herein and all documents related
    to the source code ("Material") are owned by Intel Corporation or its
    suppliers or licensors.  Title to the Material remains with Intel
    Corporation or its suppliers and licensors.  The Material is protected
    by worldwide copyright laws and treaty provisions.  No part of the
    Material may be used, copied, reproduced, modified, published, uploaded,
    posted, transmitted, distributed, or disclosed in any way without
    Intel's prior express written permission.

    No license under any patent, copyright, trade secret or other
    intellectual property right is granted to or conferred upon you by
    disclosure or delivery of the Materials, either expressly, by
    implication, inducement, estoppel or otherwise.  Any license under such
    intellectual property rights must be express and approved by Intel in
    writing.
*/

#ifndef __TBB_parallel_for_H
#define __TBB_parallel_for_H

#include <new>
#include "task.h"
#include "partitioner.h"
#include "blocked_range.h"
#include "tbb_exception.h"

namespace tbb {

namespace interface7 {
//! @cond INTERNAL
namespace internal {

    //! allocate right task with new parent
    void* allocate_sibling(task* start_for_task, size_t bytes);

    //! Task type used in parallel_for
    /** @ingroup algorithms */
    template<typename Range, typename Body, typename Partitioner>
    class start_for: public task {
        Range my_range;
        const Body my_body;
        typename Partitioner::task_partition_type my_partition;
        /*override*/ task* execute();

        //! Update affinity info, if any.
        /*override*/ void note_affinity( affinity_id id ) {
            my_partition.note_affinity( id );
        }

    public:
        //! Constructor for root task.
        start_for( const Range& range, const Body& body, Partitioner& partitioner ) :
            my_range(range),
            my_body(body),
            my_partition(partitioner)
        {
        }
        //! Splitting constructor used to generate children.
        /** parent_ becomes left child.  Newly constructed object is right child. */
        start_for( start_for& parent_, typename Partitioner::split_type& split_obj) :
            my_range(parent_.my_range, split_obj),
            my_body(parent_.my_body),
            my_partition(parent_.my_partition, split_obj)
        {
            my_partition.set_affinity(*this);
        }
        //! Construct right child from the given range as response to the demand.
        /** parent_ remains left child.  Newly constructed object is right child. */
        start_for( start_for& parent_, const Range& r, depth_t d ) :
            my_range(r),
            my_body(parent_.my_body),
            my_partition(parent_.my_partition, split())
        {
            my_partition.set_affinity(*this);
            my_partition.align_depth( d );
        }
        static void run(  const Range& range, const Body& body, Partitioner& partitioner ) {
            if( !range.empty() ) {
#if !__TBB_TASK_GROUP_CONTEXT || TBB_JOIN_OUTER_TASK_GROUP
                start_for& a = *new(task::allocate_root()) start_for(range,body,partitioner);
#else
                // Bound context prevents exceptions from body to affect nesting or sibling algorithms,
                // and allows users to handle exceptions safely by wrapping parallel_for in the try-block.
                task_group_context context;
                start_for& a = *new(task::allocate_root(context)) start_for(range,body,partitioner);
#endif /* __TBB_TASK_GROUP_CONTEXT && !TBB_JOIN_OUTER_TASK_GROUP */
                task::spawn_root_and_wait(a);
            }
        }
#if __TBB_TASK_GROUP_CONTEXT
        static void run(  const Range& range, const Body& body, Partitioner& partitioner, task_group_context& context ) {
            if( !range.empty() ) {
                start_for& a = *new(task::allocate_root(context)) start_for(range,body,partitioner);
                task::spawn_root_and_wait(a);
            }
        }
#endif /* __TBB_TASK_GROUP_CONTEXT */
        //! Run body for range, serves as callback for partitioner
        void run_body( Range &r ) { my_body( r ); }

        //! spawn right task, serves as callback for partitioner
        void offer_work(typename Partitioner::split_type& split_obj) {
            spawn( *new( allocate_sibling(static_cast<task*>(this), sizeof(start_for)) ) start_for(*this, split_obj) );
        }
        //! spawn right task, serves as callback for partitioner
        void offer_work(const Range& r, depth_t d = 0) {
            spawn( *new( allocate_sibling(static_cast<task*>(this), sizeof(start_for)) ) start_for(*this, r, d) );
        }
    };

    //! allocate right task with new parent
    // TODO: 'inline' here is to avoid multiple definition error but for sake of code size this should not be inlined
    inline void* allocate_sibling(task* start_for_task, size_t bytes) {
        task* parent_ptr = new( start_for_task->allocate_continuation() ) flag_task();
        start_for_task->set_parent(parent_ptr);
        parent_ptr->set_ref_count(2);
        return &parent_ptr->allocate_child().allocate(bytes);
    }

    //! execute task for parallel_for
    template<typename Range, typename Body, typename Partitioner>
    task* start_for<Range,Body,Partitioner>::execute() {
        my_partition.check_being_stolen( *this );
        my_partition.execute(*this, my_range);
        return NULL;
    }
} // namespace internal
//! @endcond
} // namespace interfaceX

//! @cond INTERNAL
namespace internal {
    using interface7::internal::start_for;

    //! Calls the function with values from range [begin, end) with a step provided
    template<typename Function, typename Index>
    class parallel_for_body : internal::no_assign {
        const Function &my_func;
        const Index my_begin;
        const Index my_step;
    public:
        parallel_for_body( const Function& _func, Index& _begin, Index& _step )
            : my_func(_func), my_begin(_begin), my_step(_step) {}

        void operator()( const tbb::blocked_range<Index>& r ) const {
            // A set of local variables to help the compiler with vectorization of the following loop.
            Index b = r.begin();
            Index e = r.end();
            Index ms = my_step;
            Index k = my_begin + b*ms;

#if __INTEL_COMPILER
#pragma ivdep
#if __TBB_ASSERT_ON_VECTORIZATION_FAILURE
#pragma vector always assert
#endif
#endif
            for ( Index i = b; i < e; ++i, k += ms ) {
                my_func( k );
            }
        }
    };
} // namespace internal
//! @endcond

// Requirements on Range concept are documented in blocked_range.h

/** \page parallel_for_body_req Requirements on parallel_for body
    Class \c Body implementing the concept of parallel_for body must define:
    - \code Body::Body( const Body& ); \endcode                 Copy constructor
    - \code Body::~Body(); \endcode                             Destructor
    - \code void Body::operator()( Range& r ) const; \endcode   Function call operator applying the body to range \c r.
**/

/** \name parallel_for
    See also requirements on \ref range_req "Range" and \ref parallel_for_body_req "parallel_for Body". **/
//@{

//! Parallel iteration over range with default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body ) {
    internal::start_for<Range,Body,const __TBB_DEFAULT_PARTITIONER>::run(range,body,__TBB_DEFAULT_PARTITIONER());
}

//! Parallel iteration over range with simple partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const simple_partitioner& partitioner ) {
    internal::start_for<Range,Body,const simple_partitioner>::run(range,body,partitioner);
}

//! Parallel iteration over range with auto_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const auto_partitioner& partitioner ) {
    internal::start_for<Range,Body,const auto_partitioner>::run(range,body,partitioner);
}

//! Parallel iteration over range with affinity_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, affinity_partitioner& partitioner ) {
    internal::start_for<Range,Body,affinity_partitioner>::run(range,body,partitioner);
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration over range with default partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, task_group_context& context ) {
    internal::start_for<Range,Body,const __TBB_DEFAULT_PARTITIONER>::run(range, body, __TBB_DEFAULT_PARTITIONER(), context);
}

//! Parallel iteration over range with simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const simple_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,const simple_partitioner>::run(range, body, partitioner, context);
}

//! Parallel iteration over range with auto_partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, const auto_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,const auto_partitioner>::run(range, body, partitioner, context);
}

//! Parallel iteration over range with affinity_partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_for( const Range& range, const Body& body, affinity_partitioner& partitioner, task_group_context& context ) {
    internal::start_for<Range,Body,affinity_partitioner>::run(range,body,partitioner, context);
}
#endif /* __TBB_TASK_GROUP_CONTEXT */
//@}

namespace strict_ppl {

//@{
//! Implementation of parallel iteration over stepped range of integers with explicit step and partitioner
template <typename Index, typename Function, typename Partitioner>
void parallel_for_impl(Index first, Index last, Index step, const Function& f, Partitioner& partitioner) {
    if (step <= 0 )
        internal::throw_exception(internal::eid_nonpositive_step); // throws std::invalid_argument
    else if (last > first) {
        // Above "else" avoids "potential divide by zero" warning on some platforms
        Index end = (last - first - Index(1)) / step + Index(1);
        tbb::blocked_range<Index> range(static_cast<Index>(0), end);
        internal::parallel_for_body<Function, Index> body(f, first, step);
        tbb::parallel_for(range, body, partitioner);
    }
}

//! Parallel iteration over a range of integers with a step provided and default partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, step, f, auto_partitioner());
}
//! Parallel iteration over a range of integers with a step provided and simple partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, const simple_partitioner& partitioner) {
    parallel_for_impl<Index,Function,const simple_partitioner>(first, last, step, f, partitioner);
}
//! Parallel iteration over a range of integers with a step provided and auto partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, const auto_partitioner& partitioner) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, step, f, partitioner);
}
//! Parallel iteration over a range of integers with a step provided and affinity partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, affinity_partitioner& partitioner) {
    parallel_for_impl(first, last, step, f, partitioner);
}

//! Parallel iteration over a range of integers with a default step value and default partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, static_cast<Index>(1), f, auto_partitioner());
}
//! Parallel iteration over a range of integers with a default step value and simple partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f, const simple_partitioner& partitioner) {
    parallel_for_impl<Index,Function,const simple_partitioner>(first, last, static_cast<Index>(1), f, partitioner);
}
//! Parallel iteration over a range of integers with a default step value and auto partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f, const auto_partitioner& partitioner) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, static_cast<Index>(1), f, partitioner);
}
//! Parallel iteration over a range of integers with a default step value and affinity partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f, affinity_partitioner& partitioner) {
    parallel_for_impl(first, last, static_cast<Index>(1), f, partitioner);
}

#if __TBB_TASK_GROUP_CONTEXT
//! Implementation of parallel iteration over stepped range of integers with explicit step, task group context, and partitioner
template <typename Index, typename Function, typename Partitioner>
void parallel_for_impl(Index first, Index last, Index step, const Function& f, Partitioner& partitioner, tbb::task_group_context &context) {
    if (step <= 0 )
        internal::throw_exception(internal::eid_nonpositive_step); // throws std::invalid_argument
    else if (last > first) {
        // Above "else" avoids "potential divide by zero" warning on some platforms
        Index end = (last - first - Index(1)) / step + Index(1);
        tbb::blocked_range<Index> range(static_cast<Index>(0), end);
        internal::parallel_for_body<Function, Index> body(f, first, step);
        tbb::parallel_for(range, body, partitioner, context);
    }
}

//! Parallel iteration over a range of integers with explicit step, task group context, and default partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, step, f, auto_partitioner(), context);
}
//! Parallel iteration over a range of integers with explicit step, task group context, and simple partitioner
 template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, const simple_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const simple_partitioner>(first, last, step, f, partitioner, context);
}
//! Parallel iteration over a range of integers with explicit step, task group context, and auto partitioner
 template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, const auto_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, step, f, partitioner, context);
}
//! Parallel iteration over a range of integers with explicit step, task group context, and affinity partitioner
 template <typename Index, typename Function>
void parallel_for(Index first, Index last, Index step, const Function& f, affinity_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl(first, last, step, f, partitioner, context);
}


//! Parallel iteration over a range of integers with a default step value, explicit task group context, and default partitioner
template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function& f, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, static_cast<Index>(1), f, auto_partitioner(), context);
}
//! Parallel iteration over a range of integers with a default step value, explicit task group context, and simple partitioner
 template <typename Index, typename Function, typename Partitioner>
void parallel_for(Index first, Index last, const Function& f, const simple_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const simple_partitioner>(first, last, static_cast<Index>(1), f, partitioner, context);
}
//! Parallel iteration over a range of integers with a default step value, explicit task group context, and auto partitioner
 template <typename Index, typename Function, typename Partitioner>
void parallel_for(Index first, Index last, const Function& f, const auto_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl<Index,Function,const auto_partitioner>(first, last, static_cast<Index>(1), f, partitioner, context);
}
//! Parallel iteration over a range of integers with a default step value, explicit task group context, and affinity_partitioner
 template <typename Index, typename Function, typename Partitioner>
void parallel_for(Index first, Index last, const Function& f, affinity_partitioner& partitioner, tbb::task_group_context &context) {
    parallel_for_impl(first, last, static_cast<Index>(1), f, partitioner, context);
}

#endif /* __TBB_TASK_GROUP_CONTEXT */
//@}

} // namespace strict_ppl

using strict_ppl::parallel_for;

} // namespace tbb

#if TBB_PREVIEW_SERIAL_SUBSET
#define __TBB_NORMAL_EXECUTION
#include "../serial/tbb/parallel_for.h"
#undef __TBB_NORMAL_EXECUTION
#endif

#endif /* __TBB_parallel_for_H */