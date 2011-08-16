#ifndef __CONCURRENCY_SIGNAL_HPP__
#define __CONCURRENCY_SIGNAL_HPP__

#include "concurrency/pubsub.hpp"
#include "utils.hpp"
#include <boost/bind.hpp>

/* A `signal_t` is a boolean variable, combined with a way to be notified if
that boolean variable becomes true. Typically you will construct a concrete
subclass of `signal_t`, then pass a pointer to the underlying `signal_t` to
another object which will read from or listen to it.

To check if a `signal_t` has already been pulsed, call `is_pulsed()` on it. To
be notified when it gets pulsed, construct a `signal_t::subscription_t` and pass
a callback function and the signal to watch to its constructor. The callback
will be called when the signal is pulsed.

If you construct a `signal_t::subscription_t` for a signal that's already been
pulsed, you will get an exception.

`signal_t` is generally not thread-safe, although the `wait_*()` functions are.

Although you may be tempted to, please do not add a method that "unpulses" a
`signal_t`. Part of the definition of a `signal_t` is that it does not return to
the unpulsed state after being pulsed, and some things may depend on that
property. If you want something like that, maybe you should look at something
other than `signal_t`; have you tried `resettable_cond_t`? */

struct signal_t :
    public home_thread_mixin_t
{
public:
    /* True if somebody has called `pulse()`. */
    bool is_pulsed() const {
        return pulsed;
    }

    /* Wrapper around a `publisher_t<boost::function<void()> >::subscription_t`
    */
    struct subscription_t : public home_thread_mixin_t {
        subscription_t(boost::function<void()> cb) :
            subs(cb) {
        }
        subscription_t(boost::function<void()> cb, signal_t *s) :
            subs(cb, s->publisher_controller.get_publisher()) {
            rassert(!s->is_pulsed());
        }
        void resubscribe(signal_t *s) {
            rassert(!s->is_pulsed());
            subs.resubscribe(s->publisher_controller.get_publisher());
        }
        void unsubscribe() {
            subs.unsubscribe();
        }
    private:
        publisher_t<boost::function<void()> >::subscription_t subs;
        DISABLE_COPYING(subscription_t);
    };

    /* The coro that calls `wait_lazily_ordered()` will be pushed onto the event
    queue when the signal is pulsed, but will not wake up immediately. */
    void wait_lazily_ordered() {
        if (!is_pulsed()) {
            subscription_t subs(
                boost::bind(&coro_t::notify_later_ordered, coro_t::self()),
                this);
            coro_t::wait();
        }
    }

    /* The coro that calls `wait_lazily_unordered()` will be notified soon after
    the signal has been pulsed, but not immediately. */
    void wait_lazily_unordered() {
        if (!is_pulsed()) {
            subscription_t subs(
                boost::bind(&coro_t::notify_sometime, coro_t::self()),
                this);
            coro_t::wait();
        }
    }

    /* The coro that calls `wait_eagerly()` will be woken up immediately when
    the signal is pulsed, before `pulse()` even returns.

    Note: This is dangerous! It's easy to cause race conditions by e.g.
    destroying the signal that's just been pulsed. You should probably use
    `wait_lazily_unordered()` instead; its performance will be similar once we
    optimize `notify_sometime()`. */
    void wait_eagerly() {
        if (!is_pulsed()) {
            subscription_t subs(
                boost::bind(&coro_t::notify_now, coro_t::self()),
                this);
            coro_t::wait();
        }
    }

    /* `wait()` is a deprecated synonym for `wait_lazily_ordered()`. */
    void wait() {
        wait_lazily_ordered();
    }

    void rethread(int new_thread) {
        real_home_thread = new_thread;
        publisher_controller.rethread(new_thread);
    }

protected:
    signal_t() : pulsed(false), publisher_controller(&mutex) { }
    ~signal_t() { }

    void pulse() {
        mutex_acquisition_t acq(&mutex, false);
        rassert(!is_pulsed());
        pulsed = true;
        publisher_controller.publish(&signal_t::call, &acq);
    }

private:
    static void call(boost::function<void()> &fun) {
        fun();
    }

    bool pulsed;
    mutex_t mutex;
    publisher_controller_t<boost::function<void()> > publisher_controller;
    DISABLE_COPYING(signal_t);
};

#endif /* __CONCURRENCY_SIGNAL_HPP__ */
