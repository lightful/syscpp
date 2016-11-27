
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <sstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include "Application.h"

#define DURATION_SYNC  4
#define DURATION_ASYNC 1
#define DURATION_MIXED 3

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
    syncTestRunning = true;
    if (msg.master)
    {
        timerStart('S', std::chrono::seconds(DURATION_SYNC));
        sibling->send(SyncMsg{ 1 });
    }
}

template <> void Task::onMessage(SyncMsg& msg) // sends one message after receiving another
{
    if (syncTestRunning)
        { msg.counter++; sibling->send(msg); }
    else
        app->send(SyncEnd{ msg.counter });
}

template <> void Task::onMessage(AsyncBegin&)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(DURATION_ASYNC);
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
        for(auto i = rnd(gen); i > 0; i--) { sibling->send(A{}); fstats.sntA++; }
    else
        for(auto i = rnd(gen); i > 0; i--) { sibling->send(B{}); fstats.sntB++; }
}

template <> void Task::onMessage(MixedBegin&)
{
    timerStart('A', std::chrono::seconds(DURATION_MIXED));
    mixedTestRunning = true;
    mixedTestPaused = false;
    doMixed();
}

template <> void Task::onMessage(A&)
{
    fstats.recvA++;
    if (mixedTestRunning) doMixed();
}

template <> void Task::onMessage(B&)
{
    fstats.recvB++;
    if (mixedTestRunning) doMixed();
}

template <> void Task::onMessage(MixedEnd&)
{
    app->send(fstats);
}

template <> void Task::onMessage(BreedExplode& msg)
{
    if (msg.generation <= msg.maxGenerations)
    {
        for (auto i = 0; i < msg.amount; i++)
        {
            auto child = Task::create(shared_from_this());
            child->send(BreedExplode { msg.amount, msg.generation+1, msg.maxGenerations });
            pendingChilds.insert(child); // keeps the child thread referenced (and alive)
        }
    }
    else // last generation: trigger the implosion
    {
        ancestor->send(BreedImplode { shared_from_this(), 1 });
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

    // However, with the current design, the following reset alone not always stops the thread,
    // meaning that sometimes there is an additional reference to the child. So this wouldn't
    // be enough to cause an ordered destruction unless the message is changed to use a
    // shared_ptr wrapper for BreedImplode (such references are just the temporary copies
    // in onMessage, which could be eliminated by using the heap):
    //
    // msg.child.reset();

    if (pendingChilds.empty())
    {
        if (ancestor)
            ancestor->send(BreedImplode { shared_from_this(), 1 + implosions });
        else
            app->send(BreedImplode { shared_from_this(), implosions }); // root thread
    }
}

template <> void Task::onTimer(const char& timer)
{
    if (timer == 'S') syncTestRunning = false;
    else
    {
        sibling->send(MixedEnd{});
        mixedTestRunning = false;
    }
}

void Application::onStart()
{
    std::cout << "testing performance..." << std::endl;

    snd1 = Task::create(shared_from_this());
    snd2 = Task::create(shared_from_this());
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
        tStart = std::chrono::steady_clock::now();
        bool haveParameter = argc > 1;
        if (haveParameter)
            snd1->send(BreedExplode { 2, 1, std::atoi(argv[1]) > 0? std::atoi(argv[1]) : 1 });
        else
            snd1->send(BreedExplode { 3, 1, 5 }); // by default not too many (valgrind limits friendly)
    }
}

template <> void Application::onMessage(BreedImplode& msg) // last test completed
{
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << msg.implosions << " threads created, communicated and deleted in " << elapsed << " seconds" << std::endl;
    timerStart('H', std::chrono::milliseconds(500)); // leave time for detached threads to stop (avoid memory leaks)
}

template <> void Application::onTimer(const char&)
{
    snd1->send(Task::ptr()); // remove circular reference (avoid valgrind
    snd2->send(Task::ptr()); // "possibly lost" message regarding memory)
    snd1->waitIdle();
    snd2->waitIdle();

    snd1.reset(); // remove them to wipe their reference to us preventing
    snd2.reset(); // our deletion (another valgrind "possibly lost")

    stop();
}
