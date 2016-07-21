
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <MyLib/Printer.h>
#include "Application.h"

int main(int argc, char** argv)
{
    return Application::run(argc, argv);
}

void Application::onStart()
{
    library->basicSubscriptions(shared_from_this()); // all except ReplyA and ReplyB

    library->connect(getChannel<std::shared_ptr<ReplyA>>());
    library->connect<std::shared_ptr<ReplyB>>(shared_from_this()); // (alternative syntax)

    MyLib::Channel<WantPrinter> fnDemo = library->getChannel<WantPrinter>();

    fnDemo(WantPrinter{}); // equivalent to library->send(WantPrinter{}) (but wouldn't crash if 'library' were deleted)
}

template <> void Application::onMessage(Printer::ptr& msg)
{
    printer = msg;
}

template <> void Application::onMessage(std::shared_ptr<Info>& msg)
{
    printer->send(LINE("<MyApp> received " << msg->data));

    // Programmers not very seasoned managing the objects lifecycle may be concerned
    // about the risk of 'library' being potentially deleted at the moment they
    // need to invoke its send() method. In the following example, a safeLibrary()
    // functor is used instead, which wouldn't crash even in such situation:

    if (msg->data.find("fast") != std::string::npos)
        safeLibrary(std::make_shared<RequestA>("RequestA")); // equivalent to library->send()
    else
        safeLibrary(std::make_shared<RequestB>("RequestB"));
}

template <> void Application::onMessage(std::shared_ptr<ReplyA>& msg)
{
    printer->send(LINE("<MyApp> received " << msg->data));
}

template <> void Application::onMessage(std::shared_ptr<ReplyB>& msg)
{
    printer->send(LINE("<MyApp> received " << msg->data));
}

template <> void Application::onMessage(LibraryIsTired&)
{
    printer->send(LINE("<MyApp> shutting down"));
    printer->waitIdle();
    stop();
}
