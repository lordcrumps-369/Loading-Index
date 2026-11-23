#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>
#include <new>
#include "LoadingIndexCore.h"
#include "LoadingIndexNativeGuards.h"

#ifndef LOADING_INDEX_ACTIVE
#define LOADING_INDEX_ACTIVE 0
#endif
namespace loadindex {
struct NativeVector {U32* begin;U32* end;U32* capacity;};
using Callback=bool (__cdecl *)(void*,void*);
using Enum=void (__attribute__((thiscall)) *)(void*,Callback,void*);
using Builder=NativeVector* (__attribute__((thiscall)) *)(void*,U32);
using Mutation=U32 (__attribute__((thiscall)) *)(void*,void*);
using LockMethod=void (__attribute__((thiscall)) *)(void*);
using FileId=U32 (__attribute__((thiscall)) *)(void*);
using GetVector=NativeVector* (__attribute__((thiscall)) *)(void*,const U32*);
using Sort=void (__cdecl *)(U32*,U32*,U32,U32,U32);
#ifdef LOADING_INDEX_TEST
U32 enum_address{0x7b0da0},getvector_address{0x73f1b0},callback_address{0x73d960},sort_address{0x73e7a0};
bool test_allow_guards{};
U32 test_file_id_address{};
#else
constexpr U32 enum_address=0x7b0da0,getvector_address=0x73f1b0,callback_address=0x73d960,sort_address=0x73e7a0;
#endif
struct alignas(8) Evidence {
    U32 magic{0x3149584c},version{2},size{sizeof(Evidence)},state{};
    U32 pid{},mode{LOADING_INDEX_ACTIVE},owner_thread{},hook_failure{};
    U64 frequency{},installed_ms{};
    volatile LONG64 builds{},eligible{},comparisons{},mismatches{},fallbacks{},index_rebuilds{},mutation_events{},generation{};
    volatile LONG64 indexed_files{},memberships{},unknown_files{},payload_capacity{},native_ticks{},index_ticks{},fast_ticks{},fast_builds{};
    volatile LONG64 predicted_callbacks{},full_callbacks{},max_native_list{},guard_failures{},allocation_failures{},recursive_fallbacks{},nonowner_fallbacks{},existing_fallbacks{};
    U32 mismatch_group{},mismatch_expected{},mismatch_actual{},reserved{};
    U32 first_expected[16]{},first_actual[16]{};
    U32 installation_status[5]{};
    U32 reserved2{};
};
extern "C" {__declspec(dllexport) Evidence LoadingIndexEvidence;}
Builder builder_original{};
Mutation populate_original{},update_original{},register_original{},remove_original{};
Index index;
void* indexed_manager{};
LONG64 indexed_generation{-1};
U32 indexed_headers[16]{};
bool index_complete{};
volatile LONG active_writers{};
volatile LONG stopping{};
thread_local unsigned building_depth{};
unsigned char expected_populate[16]{},expected_update[16]{},expected_register[16]{},expected_remove[16]{};

U32 pointer(const void* value){return static_cast<U32>(reinterpret_cast<std::uintptr_t>(value));}
U32 word(const void* object,std::size_t offset){U32 value{};std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,4);return value;}
void* at(void* object,std::size_t offset){return static_cast<unsigned char*>(object)+offset;}
void* virtual_method(void* object,std::size_t offset){return reinterpret_cast<void*>(word(reinterpret_cast<void*>(word(object,0)),offset));}
struct Lock {
    void* object{};
    explicit Lock(void* manager):object(at(manager,0x50)){reinterpret_cast<LockMethod>(virtual_method(object,4))(object);}
    ~Lock(){reinterpret_cast<LockMethod>(virtual_method(object,8))(object);}
};
void invalidate(){InterlockedIncrement64(&LoadingIndexEvidence.generation);InterlockedIncrement64(&LoadingIndexEvidence.mutation_events);}
thread_local unsigned mutation_depth{};
U32 invoke_mutation(Mutation original,void* self,void* file){
    if(InterlockedCompareExchange(&stopping,0,0))return original(self,file);
    const auto before_error=GetLastError();
    InterlockedIncrement(&active_writers);++mutation_depth;invalidate();SetLastError(before_error);
    const U32 result=original(self,file);const auto error=GetLastError();
    invalidate();--mutation_depth;InterlockedDecrement(&active_writers);
    SetLastError(error);return result;
}
U32 __attribute__((fastcall)) populate_entry(void* self,void*,void* file){return invoke_mutation(populate_original,self,file);}
U32 __attribute__((fastcall)) update_entry(void* self,void*,void* file){return invoke_mutation(update_original,self,file);}
U32 __attribute__((fastcall)) register_entry(void* self,void*,void* file){return invoke_mutation(register_original,self,file);}
U32 __attribute__((fastcall)) remove_entry(void* self,void*,void* file){return invoke_mutation(remove_original,self,file);}

bool read_headers(void* manager,U32 (&headers)[16]){
    // Native deque begin/end iterators (8 words), membership map (8 words).
    std::memcpy(headers,at(manager,0x58),32);
    std::memcpy(headers+8,at(manager,0x114),32);
    return true;
}
NativeVector* metadata(void* manager,U32 id){
    const U32 begin=word(manager,0x118),end=word(manager,0x11c);
    if(end<begin||(end-begin)%4||end-begin>4*1000000u||begin==end)return nullptr;
    const U32 buckets=(end-begin)/4;
    U32 node=word(reinterpret_cast<void*>(begin),4*(id%buckets));
    for(unsigned limit=0;node&&limit<200000;++limit){
        if(word(reinterpret_cast<void*>(node),4)==id)return reinterpret_cast<NativeVector*>(word(reinterpret_cast<void*>(node),8));
        node=word(reinterpret_cast<void*>(node),0);
    }
    return nullptr;
}
struct Collection {void* manager;U32 files{},unknown{};bool valid{true};};
bool supported_file_id(void* method){
#ifdef LOADING_INDEX_TEST
    return pointer(method)==test_file_id_address;
#else
    return pointer(method)==0xabaae0||pointer(method)==0x731c20;
#endif
}
bool __cdecl collect(void* file,void* opaque){
    auto& context=*static_cast<Collection*>(opaque);
    if(!context.valid)return false;
    if(!file||context.files>=100000){context.valid=false;return false;}
    const U32 ordinal=context.files++;
    auto* id_method=virtual_method(file,0x3c);
    if(!supported_file_id(id_method)){++context.unknown;return true;}
    const auto id=reinterpret_cast<FileId>(id_method)(file);
    const auto* values=metadata(context.manager,id);
    if(!values){++context.unknown;return true;}
    const auto first=pointer(values->begin),last=pointer(values->end),capacity=pointer(values->capacity);
    if(first>last||last>capacity||(last-first)%4||(last-first)>4*1000000u){context.valid=false;return false;}
    try {
        if(!index.add(ordinal,pointer(file),values->begin,(last-first)/4)){context.valid=false;return false;}
    }catch(const std::bad_alloc&){context.valid=false;InterlockedIncrement64(&LoadingIndexEvidence.allocation_failures);return false;}
    return true;
}
bool guards(void* manager){
#ifdef LOADING_INDEX_TEST
    if(test_allow_guards)return true;
#endif
    const unsigned char contains[]={0x6a,0xff,0x68,0x38,0x9a,0x15,1,0x64,0xa1,0,0,0,0};
    const unsigned char callback[]={0x64,0xa1,0,0,0,0,0x6a,0xff,0x68,0xd8,0x99,0x15,1};
    return word(manager,0)==0x1213e60&&word(manager,0xa0)==0x1213ce8&&
        native_code_intact()&&
        virtual_method(manager,0x94)==reinterpret_cast<void*>(0x7b0da0)&&
        virtual_method(at(manager,0xa0),0x78)==reinterpret_cast<void*>(0x7436b0)&&
        !std::memcmp(reinterpret_cast<void*>(0x7436b0),contains,sizeof(contains))&&
        !std::memcmp(reinterpret_cast<void*>(0x73d960),callback,sizeof(callback))&&
        !std::memcmp(reinterpret_cast<void*>(0x742f40),expected_populate,16)&&
        !std::memcmp(reinterpret_cast<void*>(0x743290),expected_update,16)&&
        !std::memcmp(reinterpret_cast<void*>(0x745ec0),expected_register,16)&&
        !std::memcmp(reinterpret_cast<void*>(0x73f840),expected_remove,16);
}
bool prepare(void* manager){
    U32 headers[16]{};read_headers(manager,headers);
    const auto generation=InterlockedCompareExchange64(&LoadingIndexEvidence.generation,0,0);
    if(indexed_manager==manager&&indexed_generation==generation&&!std::memcmp(headers,indexed_headers,sizeof(headers)))return index_complete;
    index.clear();index_complete=false;indexed_manager=manager;indexed_generation=generation;
    Collection context{manager};
    reinterpret_cast<Enum>(enum_address)(manager,collect,&context);
    LoadingIndexEvidence.indexed_files=context.files;LoadingIndexEvidence.unknown_files=context.unknown;
    LoadingIndexEvidence.memberships=index.size();LoadingIndexEvidence.payload_capacity=index.capacity_bytes();
    InterlockedIncrement64(&LoadingIndexEvidence.index_rebuilds);
    if(!context.valid||context.unknown||generation!=InterlockedCompareExchange64(&LoadingIndexEvidence.generation,0,0))return false;
    U32 after[16]{};read_headers(manager,after);
    if(std::memcmp(headers,after,sizeof(headers)))return false;
    index.finish();std::memcpy(indexed_headers,after,sizeof(after));index_complete=true;return true;
}
bool existing_group(void* manager,U32 value){
    const U32 begin=word(manager,0x104),end=word(manager,0x108);
    if(end<begin||(end-begin)%4||end-begin>4*1000000u||begin==end)return true;
    U32 node=word(reinterpret_cast<void*>(begin),4*(value%((end-begin)/4)));
    for(unsigned limit=0;node&&limit<200000;++limit){if(word(reinterpret_cast<void*>(node),4)==value)return true;node=word(reinterpret_cast<void*>(node),0);}
    return node!=0;
}
NativeVector* fast_build(void* manager,U32 value){
    auto* result=reinterpret_cast<GetVector>(getvector_address)(at(manager,0x100),&value);
    struct Context {void* manager;U32 value;NativeVector* output;} context{manager,value,result};
    const auto bounds=index.range(value);
    for(auto entry=bounds.first;entry!=bounds.second;++entry)reinterpret_cast<Callback>(callback_address)(reinterpret_cast<void*>(entry->file),&context);
    reinterpret_cast<Sort>(sort_address)(result->begin,result->end,0,0,0);
    return result;
}
NativeVector* build(void* self,U32 value){
    const auto before_error=GetLastError();auto& e=LoadingIndexEvidence;
    if(InterlockedCompareExchange(&stopping,0,0)){SetLastError(before_error);return builder_original(self,value);}
    InterlockedIncrement64(&e.builds);
    const auto thread=GetCurrentThreadId();
    InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&e.owner_thread),LONG(thread),0);
    if(e.owner_thread!=thread){InterlockedIncrement64(&e.nonowner_fallbacks);SetLastError(before_error);return builder_original(self,value);}
    if(building_depth||mutation_depth){InterlockedIncrement64(&e.recursive_fallbacks);SetLastError(before_error);return builder_original(self,value);}
    ++building_depth;
    NativeVector* result{};DWORD final_error{};
    {
        Lock lock(self);
        bool eligible=false;
        LARGE_INTEGER start{},finish{};QueryPerformanceCounter(&start);
        try {
            if(InterlockedCompareExchange(&active_writers,0,0))InterlockedIncrement64(&e.recursive_fallbacks);
            else if(!guards(self))InterlockedIncrement64(&e.guard_failures);
            else if(existing_group(self,value))InterlockedIncrement64(&e.existing_fallbacks);
            else eligible=prepare(self);
        }catch(const std::bad_alloc&){index.clear();index_complete=false;InterlockedIncrement64(&e.allocation_failures);}
        QueryPerformanceCounter(&finish);InterlockedAdd64(&e.index_ticks,finish.QuadPart-start.QuadPart);
        const auto before_generation=InterlockedCompareExchange64(&e.generation,0,0);
        if(InterlockedCompareExchange(&active_writers,0,0))eligible=false;
        if(eligible){
            InterlockedIncrement64(&e.eligible);InterlockedAdd64(&e.full_callbacks,e.indexed_files);
            const auto bounds=index.range(value);InterlockedAdd64(&e.predicted_callbacks,bounds.second-bounds.first);
        }else InterlockedIncrement64(&e.fallbacks);
        SetLastError(before_error);QueryPerformanceCounter(&start);
        if(e.mode&&eligible&&!e.mismatches){
            result=fast_build(self,value);final_error=GetLastError();
            QueryPerformanceCounter(&finish);InterlockedAdd64(&e.fast_ticks,finish.QuadPart-start.QuadPart);InterlockedIncrement64(&e.fast_builds);
        }else{
            result=builder_original(self,value);final_error=GetLastError();
            QueryPerformanceCounter(&finish);InterlockedAdd64(&e.native_ticks,finish.QuadPart-start.QuadPart);
            if(eligible&&before_generation==InterlockedCompareExchange64(&e.generation,0,0)){
                try {
                    auto predicted=index.files(value);
                    // Package precedence is defined by the native ordering
                    // routine, not by numeric pointer order.
                    reinterpret_cast<Sort>(sort_address)(predicted.data(),predicted.data()+predicted.size(),0,0,0);
                    const auto bytes=pointer(result->end)-pointer(result->begin);
                    const bool equal=bytes==predicted.size()*4&&(!bytes||!std::memcmp(predicted.data(),result->begin,bytes));
                    InterlockedIncrement64(&e.comparisons);
                    if(!equal){
                        InterlockedIncrement64(&e.mismatches);e.mismatch_group=value;e.mismatch_expected=static_cast<U32>(predicted.size());e.mismatch_actual=bytes/4;
                        for(unsigned i=0;i<16;++i){e.first_expected[i]=i<predicted.size()?predicted[i]:0;e.first_actual[i]=i<bytes/4?result->begin[i]:0;}
                    }
                }catch(const std::bad_alloc&){InterlockedIncrement64(&e.allocation_failures);}
            }
        }
        const auto result_count=(pointer(result->end)-pointer(result->begin))/4;
        if(result_count>static_cast<U64>(e.max_native_list))e.max_native_list=result_count;
    }
    --building_depth;SetLastError(final_error);return result;
}
NativeVector* __attribute__((fastcall)) builder_entry(void* self,void*,U32 value){return build(self,value);}

bool signature(U32 address,const unsigned char* expected,std::size_t size){return !std::memcmp(reinterpret_cast<void*>(address),expected,size);}
DWORD WINAPI worker(void*){
    auto& e=LoadingIndexEvidence;e.pid=GetCurrentProcessId();LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);e.frequency=frequency.QuadPart;
    HMODULE pin{};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCSTR>(worker),&pin);
#if LOADING_INDEX_ACTIVE
    char benchmark_mode[16]{};
    if(GetEnvironmentVariableA("TS2_LOADING_INDEX_MODE",benchmark_mode,sizeof(benchmark_mode))){
        if(!std::strcmp(benchmark_mode,"native")){e.mode=0;e.state=8;return 8;}
        if(!std::strcmp(benchmark_mode,"shadow"))e.mode=0;
    }
#endif
    Sleep(4500);
    if(pointer(GetModuleHandleW(nullptr))!=0x400000){e.state=2;return 2;}
    if((LOADING_INDEX_ACTIVE&&GetModuleHandleW(L"TS2-LoadingIndexValidation.asi"))||
       (!LOADING_INDEX_ACTIVE&&GetModuleHandleW(L"TS2-LoadingIndex.asi"))){e.state=7;return 7;}
    if(!native_code_intact()){e.state=3;e.hook_failure=6;return 3;}
    const U32 targets[]={0x740070,0x742f40,0x743290,0x745ec0,0x73f840};
    const unsigned char signatures[][13]={
      {0x83,0xec,0x0c,0x56,0x8b,0xf1,0x8d,0x44,0x24,0x14,0x50,0x8d,0x8e},
      {0x6a,0xff,0x68,0x78,0xa8,0x15,1,0x64,0xa1,0,0,0,0},
      {0x6a,0xff,0xe9,0xbb,0x71,0xcd,0xff,0x64,0xa1,0,0,0,0},
      {0x6a,0xff,0x68,0xb0,0xac,0x15,1,0x64,0xa1,0,0,0,0},
      {0x6a,0xff,0x68,0xde,0xa3,0x15,1,0x64,0xa1,0,0,0,0}};
    for(unsigned i=0;i<5;++i)if(!signature(targets[i],signatures[i],13)){e.state=3;e.hook_failure=i+1;return 3;}
    if(MH_Initialize()!=MH_OK){e.state=4;return 4;}
    void* replacements[]={reinterpret_cast<void*>(builder_entry),reinterpret_cast<void*>(populate_entry),reinterpret_cast<void*>(update_entry),reinterpret_cast<void*>(register_entry),reinterpret_cast<void*>(remove_entry)};
    void** originals[]={reinterpret_cast<void**>(&builder_original),reinterpret_cast<void**>(&populate_original),reinterpret_cast<void**>(&update_original),reinterpret_cast<void**>(&register_original),reinterpret_cast<void**>(&remove_original)};
    for(unsigned i=0;i<5;++i){
        auto status=MH_CreateHook(reinterpret_cast<void*>(targets[i]),replacements[i],originals[i]);e.installation_status[i]=status;
        if(status!=MH_OK){for(unsigned j=0;j<i;++j)MH_RemoveHook(reinterpret_cast<void*>(targets[j]));e.state=5;e.hook_failure=i+1;return 5;}
        MH_QueueEnableHook(reinterpret_cast<void*>(targets[i]));
    }
    if(MH_ApplyQueued()!=MH_OK){MH_DisableHook(MH_ALL_HOOKS);e.state=6;return 6;}
    std::memcpy(expected_populate,reinterpret_cast<void*>(targets[1]),16);std::memcpy(expected_update,reinterpret_cast<void*>(targets[2]),16);
    std::memcpy(expected_register,reinterpret_cast<void*>(targets[3]),16);std::memcpy(expected_remove,reinterpret_cast<void*>(targets[4]),16);
    e.installed_ms=GetTickCount64();e.state=1;return 0;
}
}
#ifndef LOADING_INDEX_TEST
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);auto thread=CreateThread(nullptr,0,loadindex::worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}else if(reason==DLL_PROCESS_DETACH){InterlockedExchange(&loadindex::stopping,1);}return TRUE;}
#endif
