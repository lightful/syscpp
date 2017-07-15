
//       Copyright Ciriaco Garcia de Celis 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <sstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include "Application.h"

#define DURATION_SYNC  std::chrono::seconds(4)
#define DURATION_ASYNC std::chrono::milliseconds(250) // uses a lot of memory
#define DURATION_MIXED std::chrono::seconds(3)
#define DURATION_MPSC  std::chrono::seconds(2)

int main(int argc, char **argv)
{
    return Application::run(argc, argv);
}

template <> void Task::onMessage(Task::ptr& peer)
{
    sibling = peer;
}

template <> void Task::onMessage(SyncBegin& msg)
{
    if (msg.master)
    {
        timerStart('S', DURATION_SYNC);
        sibling->send(SyncMsg{ 1 });
    }
}

template <> void Task::onMessage(SyncMsg& msg) // sends one message after receiving another (note
{                                              // that to a high degree, this test mostly measures
    if (!syncTestCompleted)                    // the OS context switching performance, since the
        { msg.counter++; sibling->send(msg); } // threads go idle after each message)
    else
        app->send(SyncEnd{ msg.counter });
}

template <> void Task::onMessage(AsyncBegin&)
{
    auto deadline = std::chrono::steady_clock::now() + DURATION_ASYNC;
    int counter = 0;
    while (std::chrono::steady_clock::now() < deadline)
        for (auto cnt = 0; cnt < 10000; cnt++) sibling->send(AsyncMsg { ++counter, false });
    sibling->send(AsyncMsg { ++counter, true });
}

template <> void Task::onMessage(AsyncMsg& msg)
{
    if (msg.last) app->send(AsyncEnd { msg.counter }); // both threads notify the completion when receiving the last message
}

void Task::doMixed()
{
    auto pending = sibling->pendingMessages();
    if (mixedTestPaused && (pending < 1000)) mixedTestPaused = false;
    if (!mixedTestPaused && (pending > 2000)) mixedTestPaused = true;
    if (mixedTestPaused) return;

    if (rnd(gen) < 5)
        for (auto i = rnd(gen); i >= 0; i--) { sibling->send(A{}); fstats.sntA++; }
    else
        for (auto i = rnd(gen); i >= 0; i--) { sibling->send(B{}); fstats.sntB++; }
}

template <> void Task::onMessage(MixedBegin&)
{
    timerStart('A', DURATION_MIXED);
    doMixed();
}

template <> void Task::onMessage(A&)
{
    fstats.recvA++;
    if (!mixedTestCompleted) doMixed();
}

template <> void Task::onMessage(B&)
{
    fstats.recvB++;
    if (!mixedTestCompleted) doMixed();
}

template <> void Task::onMessage(MixedEnd&)
{
    app->send(fstats);
}

template <> void Task::onMessage(MpscBegin& msg) // pendingMessages() == 1 at the beginning (just this one)
{
    int counter = 0;
    while (pendingMessages() < 2) // while MpscEnd not yet received from parent
        app->send(Mpsc { msg.id, ++counter }); // run the most intensive throughput possible
}

template <> void Task::onMessage(MpscEnd& msg)
{
    app->send(Mpsc { msg.id, -1 }); // acknowledge the end
}

template <> void Task::onMessage(BreedExplode& msg)
{
    if (msg.generation <= msg.maxGenerations)
    {
        for (auto i = 0; i < msg.amount; i++)
        {
            auto child = Task::create(weak_from_this().lock());
            child->send(BreedExplode { msg.amount, msg.generation+1, msg.maxGenerations });
            pendingChilds.insert(child); // keeps the child thread referenced (and alive)
        }
    }
    else // last generation: trigger the implosion
    {
        ancestor->send(BreedImplode { weak_from_this().lock(), 1 });
    }
}

template <> void Task::onMessage(BreedImplode& msg)
{
    implosions += msg.implosions;
    pendingChilds.erase(msg.child); // not yet deleted nor stopped (at the least referenced from 'msg.child')

    // The following code is intentionally commented to take advantage of the chaotic
    // destruction, in order to cause some threads going out of scope while still running,
    // which triggers a self-destruction in the last instance: a thread can't join itself,
    // and so chooses to detach (to avoid the application crash) and perform an asynchronous
    // exiting. In addition, this will allow to demonstrate that, when enough time is left
    // before the application ends (because the system overload) these orphan threads still
    // manage to delete their own object just before stopping (no memory is leaked):

    // Waiting for the child idle ensures that it has returned from this method besides
    // having notified us (thus having removed the reference to the grandchilds still
    // pointing it and allowing an ordered top-down destruction):
    //
    // msg.child->waitIdle();

    // The following reset alone is also enough to cause an ordered destruction unless there
    // were an additional reference to the child preventing its dead. That could have been the
    // send() call using a intermediate variable (instead of a temporal as in this example)
    // or a previous ActorThread implementation which did not optimized the rvalues movement:
    //
    // msg.child.reset();

    if (pendingChilds.empty())
    {
        if (ancestor)
            ancestor->send(BreedImplode { weak_from_this().lock(), 1 + implosions });
        else
            app->send(BreedImplode { weak_from_this().lock(), implosions }); // root thread
    }
}

template <> void Task::onTimer(const char& timer)
{
    if (timer == 'S') syncTestCompleted = true;
    else
    {
        sibling->send(MixedEnd{});
        mixedTestCompleted = true;
    }
}

void Application::onStart()
{
    std::cout << "testing performance..." << std::endl;

    snd1 = Task::create(weak_from_this().lock());
    snd2 = Task::create(weak_from_this().lock());
    snd1->send(snd2);
    snd2->send(snd1);

    tStart = std::chrono::steady_clock::now();
    snd1->send(SyncBegin{ true });
    snd2->send(SyncBegin{ false });
}

template <> void Application::onMessage(SyncEnd& msg)
{
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << msg.counter / elapsed << " synchronous messages per second" << std::endl;

    repliesCount = 0;
    tStart = std::chrono::steady_clock::now();
    snd1->send(AsyncBegin{});
    snd2->send(AsyncBegin{});
}

template <> void Application::onMessage(AsyncEnd& msg)
{
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << msg.counter / elapsed << " asynchronous messages per second and thread" << std::endl;
    repliesCount++;
    if (repliesCount == 2)
    {
        repliesCount = 0;
        tStart = std::chrono::steady_clock::now();
        snd1->send(MixedBegin{});
        snd2->send(MixedBegin{});
    }
}

template <> void Application::onMessage(MixedStats& msg)
{
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << (msg.sntA + msg.sntB + msg.recvA + msg.recvB) / elapsed << " msg/sec mixed test"
              << " sntA=" << msg.sntA << " sntB=" << msg.sntB
              << " recvA=" << msg.recvA << " recvB=" << msg.recvB << std::endl;
    repliesCount++;
    if (repliesCount == 2)
    {
        count_mpsc1 = count_mpsc2 = 0;
        repliesCount = 0;
        tStart = std::chrono::steady_clock::now();
        timerStart(123, DURATION_MPSC); // Multiple Producer Single Consumer (actually 2P1C) test
        snd1->send(MpscBegin{ 1 }); // a number is assigned to each producer
        snd2->send(MpscBegin{ 2 });
    }
}

template <> void Application::onMessage(Mpsc& msg)
{
    if (msg.counter > 0) // return as fast as possible to cope with the traffic generated from both producers
    {
        (msg.id == 1? count_mpsc1 : count_mpsc2) = msg.counter;
    }
    else // last message from a producer
    {
        auto elapsed = std::chrono::steady_clock::now() - tStart;
        (msg.id == 1? mpsc_elapsed_sc1 : mpsc_elapsed_sc2) = std::chrono::duration<double>(elapsed).count();
        repliesCount++;
        if (repliesCount == 2) // end of 0P1C phase?
        {
            auto per_second_produced_2p1c = (count_mpsc1 + count_mpsc2) / mpsc_elapsed_lap;
            auto per_second_consumed_2p1c = (count_mpsc1_lap + count_mpsc2_lap) / mpsc_elapsed_lap;
            auto sc1 = count_mpsc1 - count_mpsc1_lap;
            auto sc2 = count_mpsc2 - count_mpsc2_lap;
            auto elapsed_sc_avg = (mpsc_elapsed_sc1 + mpsc_elapsed_sc2) / 2;

            double min_msgs = std::min(std::min(std::min(count_mpsc1, count_mpsc2), count_mpsc1_lap), count_mpsc2_lap);

            double r_2p1c_p = 1.0 * std::max(count_mpsc1, count_mpsc2) / std::min(count_mpsc1, count_mpsc2);
            double r_2p1c_c = 1.0 * std::max(count_mpsc1_lap, count_mpsc2_lap) / std::min(count_mpsc1_lap, count_mpsc2_lap);
            double r_0p1c_c = 1.0 * std::max(sc1, sc2) / std::min(sc1, sc2);
            double max_ratio = std::max(std::max(r_2p1c_p, r_2p1c_c), r_0p1c_c); // hint of smoothness during the contention

            crazyScheduler = (min_msgs < 100) || (max_ratio > 50);

            std::cout << per_second_produced_2p1c << " msg/sec produced ("
                      << count_mpsc1 / mpsc_elapsed_lap << " + " << count_mpsc2 / mpsc_elapsed_lap
                      << ") 2P1C test in " << mpsc_elapsed_lap << " seconds" << std::endl;
            std::cout << per_second_consumed_2p1c << " msg/sec consumed ("
                      << count_mpsc1_lap / mpsc_elapsed_lap << " + " << count_mpsc2_lap / mpsc_elapsed_lap
                      << ") 2P1C test in " << mpsc_elapsed_lap << " seconds" << std::endl;
            std::cout << (per_second_produced_2p1c + per_second_consumed_2p1c) / 3 << " msg/sec throughput "
                      << "per thread 2P1C test (priority inversion hint: " << max_ratio << ")" << std::endl;
            std::cout << (sc1 + sc2) / elapsed_sc_avg << " msg/sec consumed ("
                      << sc1 / mpsc_elapsed_sc1 << " + " << sc2 / mpsc_elapsed_sc2 << ") 0P1C test in "
                      << elapsed_sc_avg << " seconds" << std::endl;

            repliesCount = 0;
            tStart = std::chrono::steady_clock::now(); // start next test
            bool haveParameter = argc > 1;
            if (haveParameter)
                snd1->send(BreedExplode { 2, 1, std::atoi(argv[1]) > 0? std::atoi(argv[1]) : 1 });
            else
                snd1->send(BreedExplode { 3, 1, 5 }); // by default not too many (valgrind limits friendly)
        }
    }
}

template <> void Application::onMessage(BreedImplode& msg) // last test completed
{
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << msg.implosions << " threads created, communicated and deleted in " << elapsed << " seconds" << std::endl;
    timerStart('H', std::chrono::milliseconds(500)); // leave time for detached threads to stop (avoid memory leaks)
}

template <> void Application::onTimer(const int&) // end of 2P1C phase
{
    mpsc_elapsed_lap = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    tStart = std::chrono::steady_clock::now();
    snd1->send(MpscEnd{ 1 }); // signal both producers to stop the message delivery (now starts the 0P1C
    snd2->send(MpscEnd{ 2 }); // phase, flushing the messages queued to this thread and not yet processed)
    count_mpsc1_lap = count_mpsc1;
    count_mpsc2_lap = count_mpsc2;
}

template <> void Application::onTimer(const char&) // end of application
{
    if (crazyScheduler)
        std::cout << std::endl
                  << "Advice: when running under valgrind the \"--fair-sched=yes\" option is recommended"
                  << std::endl << std::endl;

    snd1->send(Task::ptr()); // remove circular reference (avoid valgrind
    snd2->send(Task::ptr()); // "possibly lost" message regarding memory)
    snd1->waitIdle();
    snd2->waitIdle();

    snd1.reset(); // remove them to wipe their reference to us preventing
    snd2.reset(); // our deletion (another valgrind "possibly lost")

    stop();
}
