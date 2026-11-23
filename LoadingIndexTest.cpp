#define LOADING_INDEX_TEST
#include "LoadingIndexValidation.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <random>
#include <set>
#include <vector>

namespace fixture {
using namespace loadindex;
struct File {void** table;U32 id;U32 priority;};
std::vector<File*> order;
std::vector<U32> output;
NativeVector native_vector{};
unsigned callback_calls{},getvector_calls{},sort_calls{},mutation_calls{};
unsigned original_builds{},lock_depth{};
bool simulate_native{};
NativeVector* __attribute__((thiscall)) original_builder(void* manager,U32 value);
void __attribute__((thiscall)) acquire(void*){++lock_depth;SetLastError(777);}
void __attribute__((thiscall)) release(void*){assert(lock_depth);--lock_depth;SetLastError(778);}
U32 __attribute__((thiscall)) file_id(void* self){return static_cast<File*>(self)->id;}
void __attribute__((thiscall)) enumerate(void*,Callback callback,void* context){for(auto* file:order)if(!callback(file,context))break;}
NativeVector* __attribute__((thiscall)) getvector(void*,const U32*){++getvector_calls;output.clear();native_vector={};return &native_vector;}
bool __cdecl callback(void* file,void* context){
    ++callback_calls;
    struct Context {void* manager;U32 value;NativeVector* output;};
    auto& c=*static_cast<Context*>(context);auto* groups=metadata(c.manager,static_cast<File*>(file)->id);
    if(groups&&std::find(groups->begin,groups->end,c.value)!=groups->end){
        output.push_back(pointer(file));native_vector={output.data(),output.data()+output.size(),output.data()+output.capacity()};
    }
    return true;
}
void __cdecl sort(U32* begin,U32* end,U32 a,U32 b,U32 c){
    assert(!a&&!b&&!c);++sort_calls;
    if(begin!=end)std::stable_sort(begin,end,[](U32 first,U32 second){return reinterpret_cast<File*>(first)->priority>reinterpret_cast<File*>(second)->priority;});
    SetLastError(319);
}
NativeVector* __attribute__((thiscall)) original_builder(void* manager,U32 value){
    ++original_builds;
    if(!simulate_native){SetLastError(321);return &native_vector;}
    auto* result=getvector(nullptr,&value);
    struct Context {void* manager;U32 value;NativeVector* output;} context{manager,value,result};
    for(auto* file:order)callback(file,&context);
    sort(result->begin,result->end,0,0,0);return result;
}
U32 __attribute__((thiscall)) mutation(void*,void*){
    ++mutation_calls;assert(active_writers==1&&mutation_depth==1);SetLastError(317);return 0x83f719a5;
}
}
int main(int argc,char** argv){
    using namespace loadindex;
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    auto word_in=[&](){U32 value{};input.read(reinterpret_cast<char*>(&value),4);assert(input);return value;};
    const U32 file_count=word_in();std::vector<std::vector<U32>> lists(file_count);std::set<U32> distinct;
    std::vector<U32> labels(file_count);
    for(U32 i=0;i<file_count;++i){labels[i]=word_in();const auto count=word_in();lists[i].resize(count);for(auto& value:lists[i]){value=word_in();distinct.insert(value);}}
    Index relation;
    for(U32 i=0;i<file_count;++i)assert(relation.add(i,labels[i],lists[i].data(),lists[i].size()));
    relation.finish();
    U64 membership_count{};
    for(U32 i=0;i<file_count;++i)for(auto value:lists[i]){
        const auto bounds=relation.range(value);
        const auto match=std::find_if(bounds.first,bounds.second,[&](const auto& entry){return entry.ordinal==i&&entry.file==labels[i];});
        assert(match!=bounds.second);++membership_count;
    }
    std::vector<U32> values(distinct.begin(),distinct.end());
    std::mt19937 random(190711);
    for(unsigned query=0;query<1024;++query){
        const U32 value=query%2?values[random()%values.size()]:random();std::vector<U32> expected;
        for(U32 i=0;i<file_count;++i)if(std::binary_search(lists[i].begin(),lists[i].end(),value))expected.push_back(labels[i]);
        assert(relation.files(value)==expected);
    }
    // Duplicate package registrations retain callback order and multiplicity.
    Index duplicates;U32 group[]={7,7,0x80000000u,0xffffffffu};
    assert(duplicates.add(0,90,group,4));assert(duplicates.add(1,20,group,4));assert(duplicates.add(2,90,group,4));duplicates.finish();
    assert((duplicates.files(7)==std::vector<U32>{90,20,90}));
    assert((duplicates.files(0xffffffffu)==std::vector<U32>{90,20,90}));
    assert(duplicates.files(6).empty());
    U32 unsorted[]={2,1};assert(!duplicates.add(3,10,unsorted,2));
    assert(!duplicates.add(3,10,nullptr,Index::max_memberships+1));

    // Real 32-bit native ABI integration via test-only dispatch addresses.
    // Production builds keep the fixed executable addresses as constants.
    getvector_address=pointer(reinterpret_cast<void*>(fixture::getvector));
    callback_address=pointer(reinterpret_cast<void*>(fixture::callback));
    sort_address=pointer(reinterpret_cast<void*>(fixture::sort));
    enum_address=pointer(reinterpret_cast<void*>(fixture::enumerate));
    void* file_vtable[16]{};file_vtable[15]=reinterpret_cast<void*>(fixture::file_id);
    test_file_id_address=pointer(reinterpret_cast<void*>(fixture::file_id));
    fixture::File records[]={{file_vtable,5000,10},{file_vtable,5001,20},{file_vtable,5002,20}};
    auto& first=records[0];auto& second=records[1];auto& third=records[2];
    fixture::order={&first,&second,&third};
    U32 a[]={1,7},b[]={2,7},c[]={3};
    NativeVector av{a,a+2,a+2},bv{b,b+2,b+2},cv{c,c+1,c+1};
    struct Node {U32 next,id;NativeVector* groups;};
    Node n1{0,5000,&av},n2{0,5001,&bv},n3{0,5002,&cv};
    U32 buckets[17]{};buckets[5000%17]=pointer(&n1);buckets[5001%17]=pointer(&n2);buckets[5002%17]=pointer(&n3);
    alignas(8) unsigned char manager[0x200]{};
    auto setword=[&](unsigned offset,U32 value){std::memcpy(manager+offset,&value,4);};
    setword(0x118,pointer(buckets));setword(0x11c,pointer(buckets+17));
    index.clear();indexed_manager=nullptr;index_complete=false;
    assert(prepare(manager));assert(LoadingIndexEvidence.indexed_files==3);assert(LoadingIndexEvidence.memberships==5);
    const auto rebuilds=LoadingIndexEvidence.index_rebuilds;assert(prepare(manager));assert(LoadingIndexEvidence.index_rebuilds==rebuilds);
    assert((index.files(7)==std::vector<U32>{pointer(&first),pointer(&second)}));
    auto* result=fast_build(manager,7);assert(GetLastError()==319);
    std::vector<U32> expected={pointer(&second),pointer(&first)};
    assert(std::vector<U32>(result->begin,result->end)==expected);
    auto numeric=index.files(7);std::sort(numeric.begin(),numeric.end());assert(numeric!=expected);
    assert(fixture::callback_calls==2&&fixture::getvector_calls==1&&fixture::sort_calls==1);
    result=fast_build(manager,999);assert(result->begin==result->end);assert(fixture::callback_calls==2);
    // A changing/unknown package forces preparation to reject acceleration.
    n3.id=6000;invalidate();assert(!prepare(manager));assert(LoadingIndexEvidence.unknown_files==1);
    n3.id=5002;
    // Reuse the same pointers and vector boundaries with new membership data.
    c[0]=7;invalidate();assert(prepare(manager));assert(index.files(7).size()==3);
    result=fast_build(manager,7);assert((std::vector<U32>(result->begin,result->end)==std::vector<U32>{pointer(&second),pointer(&third),pointer(&first)}));
    // Removing and re-adding packages updates the native enumeration order.
    fixture::order={&third,&second};invalidate();assert(prepare(manager));assert((index.files(7)==std::vector<U32>{pointer(&third),pointer(&second)}));
    result=fast_build(manager,7);assert((std::vector<U32>(result->begin,result->end)==std::vector<U32>{pointer(&third),pointer(&second)}));
    fixture::order={&first,&third,&second};invalidate();assert(prepare(manager));assert((index.files(7)==std::vector<U32>{pointer(&first),pointer(&third),pointer(&second)}));
    // Mutation wrappers retain EAX, Win32 last-error and reentrancy/writer state.
    const auto generation=LoadingIndexEvidence.generation;
    assert(invoke_mutation(fixture::mutation,nullptr,nullptr)==0x83f719a5);assert(GetLastError()==317);
    assert(!active_writers&&!mutation_depth&&LoadingIndexEvidence.generation==generation+2);
    assert(MH_Initialize()==MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(fixture::mutation),reinterpret_cast<void*>(update_entry),reinterpret_cast<void**>(&update_original))==MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(fixture::mutation))==MH_OK);
    Mutation volatile dispatched=fixture::mutation;
    assert(dispatched(nullptr,nullptr)==0x83f719a5);assert(GetLastError()==317);
    assert(fixture::mutation_calls==2&&!active_writers&&!mutation_depth);
    assert(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);assert(MH_Uninitialize()==MH_OK);
    // Unsupported managers, other threads and recursion must return native data.
    builder_original=fixture::original_builder;
    auto& evidence=LoadingIndexEvidence;
    evidence.owner_thread=GetCurrentThreadId()+1;
    assert(build(nullptr,7)==&fixture::native_vector);assert(GetLastError()==321);
    evidence.owner_thread=GetCurrentThreadId();building_depth=1;
    assert(build(nullptr,7)==&fixture::native_vector);assert(GetLastError()==321);building_depth=0;
    mutation_depth=1;assert(build(nullptr,7)==&fixture::native_vector);assert(GetLastError()==321);mutation_depth=0;
    void* lock_table[]={nullptr,reinterpret_cast<void*>(fixture::acquire),reinterpret_cast<void*>(fixture::release)};
    setword(0x50,pointer(lock_table));
    assert(build(manager,7)==&fixture::native_vector);assert(GetLastError()==321);assert(!fixture::lock_depth);
    active_writers=1;
    assert(build(manager,7)==&fixture::native_vector);assert(GetLastError()==321);assert(!fixture::lock_depth);active_writers=0;
    assert(fixture::original_builds==5&&evidence.fast_builds==0&&evidence.guard_failures==1);
    // Run the full production wrapper with test-injected native dispatch. The
    // compatibility guard itself stays real/strict in the compiled game DLL.
    U32 group_buckets[17]{};setword(0x104,pointer(group_buckets));setword(0x108,pointer(group_buckets+17));
    test_allow_guards=true;fixture::simulate_native=true;
    const auto before_callbacks=fixture::callback_calls,before_builds=fixture::original_builds;
    const auto before_comparisons=evidence.comparisons,before_fast=evidence.fast_builds;
    result=build(manager,1);assert(GetLastError()==319);assert(!fixture::lock_depth);
    assert((std::vector<U32>(result->begin,result->end)==std::vector<U32>{pointer(&first)}));
    if(LOADING_INDEX_ACTIVE){assert(evidence.fast_builds==before_fast+1);assert(fixture::callback_calls==before_callbacks+1);assert(fixture::original_builds==before_builds);}
    else{assert(evidence.comparisons==before_comparisons+1);assert(evidence.mismatches==0);assert(fixture::callback_calls==before_callbacks+3);}
    result=build(manager,7);assert(GetLastError()==319);
    assert((std::vector<U32>(result->begin,result->end)==std::vector<U32>{pointer(&third),pointer(&second),pointer(&first)}));
    assert(evidence.mismatches==0&&!fixture::lock_depth);
    const auto comparisons_after=evidence.comparisons;
    evidence.mode=0;result=build(manager,7);assert(GetLastError()==319);
    assert(evidence.comparisons==comparisons_after+1&&evidence.mismatches==0);
    fixture::simulate_native=false;stopping=1;
    const auto builds_before_stop=evidence.builds;
    assert(build(nullptr,7)==&fixture::native_vector&&GetLastError()==321);
    assert(evidence.builds==builds_before_stop&&!fixture::lock_depth);stopping=0;
    test_allow_guards=false;
    std::printf("PASS: %u native snapshot files; %llu memberships; 1024 independent forward scans; duplicate registrations/order; high unsigned keys; missing metadata; in-place mutation; removal/re-addition; native callback/getvector/sort ABI; MinHook mutation trampoline. Evidence bytes: %zu\n",file_count,static_cast<unsigned long long>(membership_count),sizeof(Evidence));
}
