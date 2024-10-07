#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity):cap(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t len=min(remaining_capacity(),data.size());
    dataStream.append(BufferList(data.substr(0, len)));
    writeByte+=len;
    return len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    // 这里仅提供不安全的字符串复制，不判断边界条件，在read函数中进行判断
    // if(len>0)
    return std::string(dataStream.concatenate().data(), len);
    // std::string str=dataStream.concatenate();
    // if(len>=str.size()){
    //     return str;
    // }else{
    //     // 直接从 std::string 的地址处返回前 len 个字符并不是一个安全或推荐的做法。
    //     // 使用 substr 方法或其他标准方法来操作字符串，以确保代码的安全性和正确性。
    //     return std::string(str.data(), len);
    //     // return str.substr(0,len);
    //     // 注意这个函数为const，因此不能调用一个非const的成员函数。
    //     // dataStream.remove_prefix(len);
    // }
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    dataStream.remove_prefix(len);
    readByte+=len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t length=min(dataStream.size(),len);
    std::string str=peek_output(length);
    pop_output(length);
    return str;
}

void ByteStream::end_input() {
    _end_input=true;
}

bool ByteStream::input_ended() const { return _end_input; }

size_t ByteStream::buffer_size() const { return dataStream.size(); }

bool ByteStream::buffer_empty() const { return !buffer_size(); }

bool ByteStream::eof() const { return input_ended()&&buffer_empty(); }

size_t ByteStream::bytes_written() const { return writeByte; }

size_t ByteStream::bytes_read() const { return readByte; }

size_t ByteStream::remaining_capacity() const { return cap-buffer_size(); }
