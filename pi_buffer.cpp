#include "pi_buffer.h"

pi_buffer::pi_buffer(int m_size)
{
    //ctor
    char_buf=new char[m_size];
    icnt=0;
    ocnt=0;
    char_count=0;
    sv_size=m_size;
}

pi_buffer::~pi_buffer()
{
    delete char_buf;
    //dtor
}

void pi_buffer::add_char(char ch){
    if(!full()){
        char_count++;
        char_buf[icnt++]=ch;
        if(icnt==sv_size)icnt=0;
    }
    else{
        cout<<"**** buffer overflow"<<endl;
    }
}

char pi_buffer::get_char(){
    char retval=0;
    if(!empty()){
        char_count--;
        retval=char_buf[ocnt++];
        if(ocnt==sv_size)ocnt=0;
    }
    return retval;
}

bool pi_buffer::empty(){
    return(!char_count);
}

bool pi_buffer::full(){
    return (char_count==sv_size);
}

int pi_buffer::get_count(){
    return char_count;
}
