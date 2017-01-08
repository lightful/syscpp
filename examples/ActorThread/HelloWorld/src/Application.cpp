
//       Copyright Ciriaco Garcia de Celis 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "Application.h"

int main(int argc, char** argv)
{
    return Application::run(argc, argv); // blocking call (Application::weak_from_this() will not expire until stop())
}

Application::Application(int /*argc*/, char** /*argv*/)
{
}

void Application::onStart()
{
    printer = Printer::create();
    printer->send(LINE("<application> print test page"));

    world = World::create(weak_from_this().lock()); // lock() returns a non empty reference (see comment in main())
    world->send(printer);

    world->send(2016);
    world->send(Kiosk { "latest newspaper" });
    world->send(Gallery { "La persistencia de la memoria", "Dali" });
    world->send(Bank { 50, "savings" });

    timerStart(123, std::chrono::seconds(1));
}

void Application::onMessage(Newspaper& msg)
{
    printer->send(LINE("<application> is responded: " << msg.name ));
}

void Application::onMessage(Picture& msg)
{
    printer->send(LINE("<application> is responded: " << msg.width << "x" << msg.height << " picture"));
}

void Application::onMessage(Money& msg)
{
    printer->send(LINE("<application> is responded: " << msg.amount << " euros"));
}

void Application::onTimer(const int&)
{
    stop(123); // valid call (self-terminate request) from threads started by run(); with optional exit code
}

void Application::onStop()
{
    printer->send(LINE("<application> exiting"));
    printer->waitIdle();
    world.reset();
}
