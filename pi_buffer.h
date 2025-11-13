#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <string>
#include <algorithm>
#include <fstream>

#include <climits>
#include <numeric>
#include <functional>
using namespace std;

#ifndef PI_BUFFER_H
#define PI_BUFFER_H

class pi_buffer
{
    public:
        pi_buffer(int size);
        virtual ~pi_buffer();
        void add_char(char);
        char get_char();
        bool empty();
        bool full();
        int get_count();
    protected:
    private:
    char *char_buf;
    int icnt;
    int ocnt;
    int char_count;
    int sv_size;
};

#endif // PI_BUFFER_H
