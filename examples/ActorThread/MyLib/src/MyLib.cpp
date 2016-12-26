
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <memory>
#include "MyLib/MyLib.h"

template <> void MyLib::onMessage(WantPrinter&)
{
    printer->send(LINE("<MyLib> sending printer to client"));
    publish(printer);

    // some activity to spend ink
    timerStart('A', std::chrono::nanoseconds(333333333), TimerCycle::Periodic);
    timerStart(std::string("faster event"), std::chrono::seconds(1), TimerCycle::Periodic); // char const* const& is ugly
    timerStart<std::string>("slower event", std::chrono::seconds(2), TimerCycle::Periodic); // alternative syntax
    timerStart(LibraryIsTired{}, std::chrono::seconds(8));

    auto billingAddress = callback<const std::shared_ptr<Billing>>(); // billingAddress(bill) is equivalent to publish(bill)
    timerStart(bills, std::chrono::seconds(1), billingAddress, TimerCycle::Periodic); // periodic invocation
}

template <> void MyLib::onMessage(std::shared_ptr<RequestA>& msg)
{
    printer->send(LINE("<MyLib> received " << msg->data));
    publish(std::make_shared<ReplyA>("reply to " + msg->data));
    bills->count++;
}

template <> void MyLib::onMessage(std::shared_ptr<RequestB>& msg)
{
    printer->send(LINE("<MyLib> received " << msg->data));
    auto reply = std::make_shared<ReplyB>("reply to " + msg->data);
    publish(std::move(reply)); // example invalidating 'reply'
    if (reply) printer->send(LINE("<MyLib> no subscriptor to replies for " << msg->data));
    bills->count++;
}

template <> void MyLib::onTimer(const std::string& whatEvent)
{
    publish(std::unique_ptr<Info>(new Info { whatEvent }));
}

template <> void MyLib::onTimer(const char& acter)
{
    printer->send(LINE("<MyLib> beat " << acter));
}

template <> void MyLib::onTimer(const LibraryIsTired& seriously)
{
    publish(seriously);
}
