#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}



//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data1, const size_t index, const bool eof) {
    if(eof){
        _inputEOF=eof;
        _endInputIndex=index+data1.length();
    }
    // cout<<"data   "<<index<<"  "<<data.length()+index<<endl;
    // 这边可能出现数据超过win的大小，截断---之前的实验中没考虑到
    string data = data1;
    if(index>_nextIndex+_capacity){
        return;
    }else if(index+data.length()>_nextIndex+_capacity){
        data = data.substr(0,_nextIndex+_capacity-index);
    }
    size_t remainCapacity=_capacity-_output.buffer_size();//可用空间
    // 剩余空间包括整个容量的剩余空间+未组装的空间
    if((index+data.length())>_nextIndex&&remainCapacity>0){//有数据能存
        size_t startIndex=max(_nextIndex,index);//起始坐标
        size_t endIndex=index+data.length();//结束坐标
        size_t unassembledUsedSpace=0;// 前面遍历过的字符串用的空间
        size_t writesize=0;//要写多少字符

        //------------------------------------------------------------------------------------------------------找到写的位置,写进去
        auto it=_unassembled.begin();
        while(it!=_unassembled.end()&&(startIndex<endIndex)){//有东西写
            size_t leftIndex=it->first;
            size_t rightIndex=it->first+it->second.length();
            
            if(startIndex<leftIndex){//起点在当前字符串的左边，将当前字符串能写的全写进去
                writesize=min(remainCapacity-unassembledUsedSpace,endIndex-startIndex);
                if(writesize>0){
                    _unassembled[startIndex]=data.substr(startIndex-index,writesize);
                    // cout<<"1111--"<<startIndex<<"    "<<startIndex-index<<"    "<<writesize<<"   "<<endl;
                    unassembledUsedSpace+=writesize;
                    endIndex=startIndex+writesize;
                }
                break;
            }else if(startIndex<rightIndex){//调整起点
                startIndex=rightIndex;
                // cout<<"startEnd   "<<startIndex<<" "<<endIndex<<endl;
            }
            unassembledUsedSpace+=it->second.length();
            it++;
        }
        //------------------------------------------------------------------------------------------------------清除重复的字符串
        if(it!=_unassembled.end()){
            while(it!=_unassembled.end()){//写进去了就要清除重复字符串
                if((it->first+it->second.length())<=endIndex){
                    it=_unassembled.erase(it);
                }else{
                    break;
                }
            }
            while(it!=_unassembled.end()){//清除完重复字符串，接着写后面的字符串
                writesize=remainCapacity-unassembledUsedSpace;//剩余可写空间
                if(writesize==0){
                    it=_unassembled.erase(it);
                    it++;
                }else{
                    if(it->first<endIndex){//索引在endIndex的左边
                        size_t t=it->second.length()-(endIndex-it->first);
                        if(writesize<t){
                            string tmp=it->second.substr(endIndex-it->first,writesize);
                            it=_unassembled.erase(it);
                            _unassembled[endIndex]=tmp;
                            unassembledUsedSpace+=writesize;
                        }else{
                            string tmp=it->second.substr(endIndex-it->first,writesize);
                            it=_unassembled.erase(it);
                            _unassembled[endIndex]=tmp;
                            unassembledUsedSpace+=t;
                        }
                        
                    }else{//右边
                        if(writesize<it->second.length()){//不够写
                            unassembledUsedSpace+=writesize;
                            if(it->first>=endIndex){
                                _unassembled[it->first]=it->second.substr(0,writesize);
                                it++;//进入下一个节点
                            }
                        }else{//够写
                            unassembledUsedSpace+=it->second.length();
                            it++;//进入下一个节点
                        }
                    }
                }
            }

        }else if(startIndex<endIndex){//没有写进去一定是最后的了
            writesize=min(remainCapacity-unassembledUsedSpace,endIndex-startIndex);
            if(writesize>0){
                // cout<<555555<<endl;
                _unassembled[startIndex]=data.substr(startIndex-index,writesize);
                // cout<<555555<<endl;
                // cout<<"2222--"<<startIndex<<"    "<<startIndex-index<<"    "<<writesize<<"   "<<endl;
                unassembledUsedSpace+=writesize;
            }
        }
        //然后再进行组装
        it=_unassembled.begin();
        while(it!=_unassembled.end()){
            // if(_nextIndex==it->first)
            // cout<<">>>>>TTT    "<<_nextIndex<<"  "<<it->first<<"   "<<_nextIndex+it->second.length()<<endl;
            
            if(it->first==_nextIndex){
                writesize=it->second.length();
                _output.write(it->second);
                // cout<<_output.read(_output.buffer_size())<<endl;
                _nextIndex+=writesize;
                unassembledUsedSpace-=writesize;
                it=_unassembled.erase(it);
            }else{
                break;
            }
            // if()
        }
        _unassembledSize=unassembledUsedSpace;
    }
    // cout<<"EOF    "<<_inputEOF<<"  "<<_unassembledSize<<" "<<_endInputIndex<<endl;
    if(_inputEOF&&_unassembledSize==0&&_nextIndex==_endInputIndex){
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembledSize; }

bool StreamReassembler::empty() const { return !_unassembledSize; }
