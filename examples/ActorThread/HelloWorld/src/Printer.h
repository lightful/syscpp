
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef PRINTER_H
#define PRINTER_H

#include <string>
#include <sstream>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sys++/ActorThread.hpp>
#include <sys++/String.hpp>

#define LINE(text) VA_STR("<thread " << std::this_thread::get_id() << "> " << text)

class Printer : public ActorThread<Printer> // a dedicated printing thread prevents a mixed output
{
    friend ActorThread<Printer>;

    Printer() : start(std::chrono::system_clock::now())
    {
        LINE(""); // dummy use (otherwise the helgrind tool reports false std::ostream warnings)
    }

    void onStart()
    {
        onMessage(LINE("<printer> power on")); // safe call because is the same thread
    }

    void onMessage(const std::string& textLine) // also outputs the relative time
    {
        auto elapsed = std::chrono::duration<double>(std::chrono::system_clock::now() - start);
        std::cout << std::setw(15) << std::fixed << std::setprecision(9) << elapsed.count()
                  << " " << textLine << std::endl;
    }

    std::chrono::system_clock::time_point start;
};

#endif /* PRINTER_H */
