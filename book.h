#ifndef BOOK_H
#define BOOK_H
#include <string>

struct Book
{
    std::string name;
    std::string auth;
    std::string path;
    std::string izd;
    int pages;
    int year;
};

#endif // BOOK_H
