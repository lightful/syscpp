
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef STRING_H_
#define STRING_H_

#include <string>
#include <cctype>
#include <algorithm>
#include <sstream>

#define VA_STR(x) static_cast<std::ostringstream&>(std::ostringstream().flush() << x).str()

struct String // a "namespace" not requiring a cpp nor inlining to avoid "unused function" warnings
{
    static std::string& tolower(std::string& str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str;
    }

    static std::string& toupper(std::string& str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }

    static std::string& ltrim(std::string& str)
    {
        std::string::iterator i = str.begin();
        while (i != str.end()) if (!std::isspace(*i)) break; else ++i;
        str.erase(str.begin(), i);
        return str;
    }

    static std::string& rtrim(std::string& str)
    {
        std::string::iterator i = str.end();
        while (i != str.begin()) if (!std::isspace(*(--i))) { ++i; break; }
        str.erase(i, str.end());
        return str;
    }

    static std::string& trim(std::string& str)
    {
        return ltrim(rtrim(str));
    }

    static std::string right(const std::string& str, std::string::size_type count)
    {
        return str.substr(str.size() - std::min(count, str.size()));
    }

    static std::string& replaceAll(std::string& str, const std::string& sWhat, const std::string& sWith)
    {
        std::string::size_type lookHere = 0;
        std::string::size_type foundHere;
        if (sWhat.length()) while ((foundHere = str.find(sWhat, lookHere)) != std::string::npos)
        {
            str.replace(foundHere, sWhat.size(), sWith);
            lookHere = foundHere + sWith.size();
        }
        return str;
    }

    template <typename T> static void split(const std::string& str, const char delimiter, T& result, bool trimmed = true)
    {
        std::istringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter))
        {
            if (trimmed) trim(item);
            result.emplace_back(item);
        }
        if (str.length() && (str.back() == delimiter)) result.emplace_back(std::string());
    }
};

#endif /* STRING_H_ */
