#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace loadindex {
using U32=std::uint32_t;
using U64=std::uint64_t;
struct Membership {U32 value{},ordinal{},file{};};
class Index {
    std::vector<Membership> entries_;
public:
    static constexpr std::size_t max_memberships=1000000;
    void clear(){std::vector<Membership>().swap(entries_);}
    bool add(U32 ordinal,U32 file,const U32* begin,std::size_t count){
        if(count>max_memberships-entries_.size())return false;
        if(count&&!std::is_sorted(begin,begin+count))return false;
        for(std::size_t i=0;i<count;++i){
            if(i&&begin[i]==begin[i-1])continue;
            entries_.push_back({begin[i],ordinal,file});
        }
        return true;
    }
    void finish(){
        std::sort(entries_.begin(),entries_.end(),[](const Membership& a,const Membership& b){
            return a.value<b.value||(a.value==b.value&&a.ordinal<b.ordinal);
        });
    }
    auto range(U32 value) const {
        auto begin=std::lower_bound(entries_.begin(),entries_.end(),value,[](const Membership& entry,U32 needle){return entry.value<needle;});
        auto end=std::upper_bound(begin,entries_.end(),value,[](U32 needle,const Membership& entry){return needle<entry.value;});
        return std::make_pair(begin,end);
    }
    std::vector<U32> files(U32 value) const {
        const auto bounds=range(value);std::vector<U32> out;
        out.reserve(static_cast<std::size_t>(bounds.second-bounds.first));
        for(auto it=bounds.first;it!=bounds.second;++it)out.push_back(it->file);
        return out;
    }
    std::size_t size() const{return entries_.size();}
    std::size_t capacity_bytes() const{return entries_.capacity()*sizeof(Membership);}
};
}
