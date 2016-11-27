
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef APPLICATION_H
#define APPLICATION_H

#include <sys++/ActorThread.hpp>
#include <MyLib/MyLib.h>

class Application : public ActorThread<Application>
{
    friend ActorThread<Application>;

    Application(int, char**) : library(MyLib::create()), safeLibrary(library) {}

    void onStart();
    template <typename Any> void onMessage(Any&);

    MyLib::ptr library;
    MyLib::Gateway safeLibrary;
    Printer::ptr printer;
};

#endif /* APPLICATION_H */
