#pragma once
#include <cassert>
#include <atomic>
#include <cstddef>
#include <new>

namespace collections::concurrency::GC {
    namespace details {

        using hazard_ptr_handle = void*;

        class hazard_pointer_guard {
        public:
            template <typename T>
            T* operator=( T* ptr ) noexcept
            {
                set( ptr );
                return ptr;
            }

            std::nullptr_t operator=( std::nullptr_t ) noexcept
            {
                clear();
                return nullptr;
            }

            template<typename T>
            void set(T* ptr) noexcept {
                m_hp.store(reinterpret_cast<hazard_ptr_handle>(ptr), std::memory_order::memory_order_release);
            }

            void clear() noexcept
            {
                clear(std::memory_order_release );
            }

            void clear(std::memory_order order ) noexcept
            {
                m_hp.store( nullptr, order );
            }
        public:
            hazard_pointer_guard* next { nullptr };
        private:
            std::atomic<hazard_ptr_handle> m_hp { nullptr };
        };

        template <size_t Capacity>
        class guard_array
        {
        public:
            static size_t const c_nCapacity = Capacity;

        public:
            guard_array()
                    : arr_{ nullptr }
            {}

            static constexpr size_t capacity()
            {
                return c_nCapacity;
            }

            hazard_pointer_guard* operator[]( size_t idx ) const noexcept
            {
                assert( idx < capacity());
                return arr_[idx];
            }

            template <typename T>
            void set( size_t idx, T* ptr ) noexcept
            {
                assert( idx < capacity());
                assert( arr_[idx] != nullptr );

                arr_[idx]->set( ptr );
            }

            void clear( size_t idx ) noexcept
            {
                assert( idx < capacity());
                assert( arr_[idx] != nullptr );

                arr_[idx]->clear();
            }

            hazard_pointer_guard* release( size_t idx ) noexcept
            {
                assert( idx < capacity());

                hazard_pointer_guard* g = arr_[idx];
                arr_[idx] = nullptr;
                return g;
            }

            void reset( size_t idx, hazard_pointer_guard* g ) noexcept
            {
                assert( idx < capacity());
                assert( arr_[idx] == nullptr );

                arr_[idx] = g;
            }

        private:
            hazard_pointer_guard*  arr_[c_nCapacity];
        };

        typedef void (* free_retired_ptr_func )( void * );

        /// Retired pointer
        /**
            Pointer to an object that is ready to delete.
        */
        struct retired_ptr {
            /// Pointer type
            typedef void *          pointer;

            union {
                pointer                 m_p;        ///< retired pointer
                uintptr_t               m_n;
            };
            free_retired_ptr_func   m_funcFree; ///< pointer to the destructor function

            /// Comparison of two retired pointers
            static bool less( const retired_ptr& p1, const retired_ptr& p2 ) noexcept
            {
                return p1.m_p < p2.m_p;
            }

            /// Default ctor initializes pointer to \p nullptr
            retired_ptr() noexcept
                    : m_p( nullptr )
                    , m_funcFree( nullptr )
            {}

            /// Copy ctor
            retired_ptr( retired_ptr const& rp ) noexcept
                    : m_p( rp.m_p )
                    , m_funcFree( rp.m_funcFree )
            {}

            /// Ctor
            retired_ptr( pointer p, free_retired_ptr_func func ) noexcept
                    : m_p( p )
                    , m_funcFree( func )
            {}

            /// Typecasting ctor
            template <typename T>
            retired_ptr( T* p, free_retired_ptr_func func) noexcept
                    : m_p( reinterpret_cast<pointer>(p))
                    , m_funcFree( func )
            {}

            /// Assignment operator
            retired_ptr& operator =( retired_ptr const& s) noexcept
            {
                m_p = s.m_p;
                m_funcFree = s.m_funcFree;
                return *this;
            }

            /// Invokes destructor function for the pointer
            void free()
            {
                assert( m_funcFree );
                assert( m_p );
                m_funcFree( m_p );

                //CDS_STRICT_DO( clear());
            }

            /// Checks if the retired pointer is not empty
            explicit operator bool() const noexcept
            {
                return m_p != nullptr;
            }

            /// Clears retired pointer without \p free() call
            void clear()
            {
                m_p = nullptr;
                m_funcFree = nullptr;
            }
        };

        static inline bool operator <( const retired_ptr& p1, const retired_ptr& p2 ) noexcept
        {
            return retired_ptr::less( p1, p2 );
        }

        static inline bool operator ==( const retired_ptr& p1, const retired_ptr& p2 ) noexcept
        {
            return p1.m_p == p2.m_p;
        }

        static inline bool operator !=( const retired_ptr& p1, const retired_ptr& p2 ) noexcept
        {
            return !(p1 == p2);
        }



        /// \brief Hazard pointer storage per thread
        class thread_hp_storage {
        public:
            thread_hp_storage(hazard_pointer_guard* guardArr, std::size_t nSize) noexcept:
                    m_freeHead{guardArr}, m_array{guardArr}, m_capacity{nSize} {
                new( guardArr ) hazard_pointer_guard[nSize];

                for ( hazard_pointer_guard *pEnd = guardArr + nSize - 1; guardArr < pEnd; ++guardArr )
                    guardArr -> next = guardArr + 1;
                guardArr -> next = nullptr;

            }

            thread_hp_storage() = delete;
            thread_hp_storage(const thread_hp_storage& other) = delete;
            thread_hp_storage(thread_hp_storage&& other) = delete;

            std::size_t capacity() const noexcept {
                return m_capacity;
            }

            hazard_pointer_guard* alloc() {
                hazard_pointer_guard* allocGuard = m_freeHead;
                m_freeHead = allocGuard -> next;
                return allocGuard;
            }

            void free(hazard_pointer_guard* guard) noexcept {
                if(guard) {
                    guard -> clear();
                    guard -> next = m_freeHead;
                    m_freeHead = guard;
                }
            }

        private:
            hazard_pointer_guard* m_freeHead;
            const hazard_pointer_guard* m_array;
            const std::size_t m_capacity;
        };

        class retired_array {
        public:
            retired_array(retired_ptr *arr, size_t capacity) noexcept
                    : current_(arr), last_(arr + capacity), retired_(arr)
#       ifdef CDS_ENABLE_HPSTAT
            , retire_call_count_(0)
#       endif
            {}

            retired_array() = delete;

            retired_array(retired_array const &) = delete;

            retired_array(retired_array &&) = delete;

            size_t capacity() const noexcept {
                return last_ - retired_;
            }

            size_t size() const noexcept {
                return current_.load(std::memory_order::memory_order_relaxed) - retired_;
            }

            bool push(retired_ptr &&p) noexcept {
                retired_ptr *cur = current_.load(std::memory_order::memory_order_relaxed);
                *cur = p;
                //CDS_HPSTAT(++retire_call_count_);
                current_.store(cur + 1, std::memory_order::memory_order_relaxed);
                return cur + 1 < last_;
            }

            retired_ptr *first() const noexcept {
                return retired_;
            }

            retired_ptr *last() const noexcept {
                return current_.load(std::memory_order::memory_order_relaxed);
            }

            void reset(size_t nSize) noexcept {
                current_.store(first() + nSize, std::memory_order::memory_order_relaxed);
            }

            void interthread_clear() {
                current_.exchange(first(), std::memory_order::memory_order_acq_rel);
            }

            bool full() const noexcept {
                return current_.load(std::memory_order::memory_order_relaxed) == last_;
            }

            static size_t calc_array_size(size_t capacity) {
                return sizeof(retired_ptr) * capacity;
            }

        private:
            std::atomic<retired_ptr* > current_;
            retired_ptr* const last_;
            retired_ptr* const retired_;
#   ifdef CDS_ENABLE_HPSTAT
            public:
        size_t  retire_call_count_;
#   endif
        };


        struct thread_data {
            thread_hp_storage hazards;
            retired_array retired;

            /// TODO(a.raag): padding or alignas single or double ??
            alignas(64) std::atomic<unsigned int> a_sync;

            thread_data(hazard_pointer_guard* guardArr, std::size_t nGuards, retired_ptr* retired_arr, size_t retired_capacity):
                    hazards(guardArr, nGuards), retired(retired_arr, retired_capacity), a_sync{ 0 } {}
            thread_data() = delete;
            thread_data(thread_data const &) = delete;
            thread_data(thread_data&& ) = delete;

            void sync() {
                a_sync.fetch_add(1, std::memory_order::memory_order_acq_rel);
            }
        };

        class TLSManager {
        public:
            static thread_data* getTLS() {
                return m_tlsData;
            }

            static void setTLS(thread_data* td) {
                m_tlsData = td;
            }

        private:
            static thread_local thread_data* m_tlsData;
        };

        thread_local thread_data* TLSManager::m_tlsData = nullptr;

        /// \p smr::scan() strategy
        enum class scan_type {
            classic,    ///< classic scan as described in Michael's works (see smr::classic_scan())
            inplace     ///< inplace scan without allocation (see smr::inplace_scan())
        };

        class basic_smr {
        private:
            template<typename TLSManager>
            friend class Generic_SMR;

            struct thread_record: thread_data
            {
                // next hazard ptr record in list
                thread_record*                      next_ = nullptr;
                // Owner thread_record; nullptr - the record is free (not owned)
                std::atomic<thread_record*>  owner_rec_;
                // true if record is free (not owned)
                std::atomic<bool>               free_{ false };

                thread_record(hazard_pointer_guard* guards, size_t guard_count, retired_ptr* retired_arr, size_t retired_capacity )
                        : thread_data( guards, guard_count, retired_arr, retired_capacity ), owner_rec_(this)
                {}
            };

        public:
            static basic_smr& getInstance() {
                return *m_instance;
            }

            static void construct(
                    std::size_t nHazardPtrCount = 0,     ///< Hazard pointer count per thread
                    std::size_t nMaxThreadCount = 0,     ///< Max count of simultaneous working thread in your application
                    std::size_t nMaxRetiredPtrCount = 0, ///< Capacity of the array of retired objects for the thread
                    scan_type nScanType = scan_type::inplace   ///< Scan type (see \ref scan_type enum)
            );

            // for back-copatibility
            static void Construct(
                    size_t nHazardPtrCount = 0,     ///< Hazard pointer count per thread
                    size_t nMaxThreadCount = 0,     ///< Max count of simultaneous working thread in your application
                    size_t nMaxRetiredPtrCount = 0, ///< Capacity of the array of retired objects for the thread
                    scan_type nScanType = scan_type::inplace   ///< Scan type (see \ref scan_type enum)
            ) {
                construct(nHazardPtrCount, nMaxThreadCount, nMaxRetiredPtrCount, nScanType);
            }

            static void destruct(
                    bool bDetachAll = false     ///< Detach all threads
            );
        private:
            basic_smr(
                    std::size_t nHazardPtrCount,     ///< Hazard pointer count per thread
                    std::size_t nMaxThreadCount,     ///< Max count of simultaneous working thread in your application
                    std::size_t nMaxRetiredPtrCount, ///< Capacity of the array of retired objects for the thread
                    scan_type nScanType         ///< Scan type (see \ref scan_type enum)
            );

            ~basic_smr();

            thread_record* create_thread_data();
            static void destroy_thread_data(thread_record* record);

            basic_smr::thread_record* alloc_thread_data();
            void free_thread_data(thread_record* threadRecord, bool callHelpScan);

        private:
            std::atomic<thread_record*> m_threadList;
            static basic_smr* m_instance;
        };


        basic_smr* basic_smr::m_instance = nullptr;

        namespace {
            constexpr std::size_t hazard_pointers_per_thread = 8;
        }


        void
        basic_smr::construct(std::size_t nHazardPtrCount,
                             std::size_t nMaxThreadCount,
                             std::size_t nMaxRetiredPtrCount,
                             details::scan_type nScanType) {
            if(!m_instance)
                m_instance = new basic_smr(nHazardPtrCount, nMaxThreadCount, nMaxRetiredPtrCount, nScanType);
        }

        void basic_smr::destruct(bool bDetachAll) {
            if(m_instance) {

                m_instance -> ~basic_smr();
                delete m_instance;
                m_instance = nullptr;
            }
        }

        basic_smr::basic_smr(std::size_t nHazardPtrCount,
                             std::size_t nMaxThreadCount,
                             std::size_t nMaxRetiredPtrCount,
                             details::scan_type nScanType) {
            m_threadList.store(nullptr, std::memory_order::memory_order_release);
        }

        basic_smr::~basic_smr() {

        }



        basic_smr::thread_record* basic_smr::create_thread_data() {
            const std::size_t guard_array_size = sizeof(hazard_pointer_guard) * hazard_pointers_per_thread;
            const std::size_t retired_array_size = 0; /// todo calc
            const std::size_t allocSize = sizeof(thread_record) + guard_array_size + retired_array_size;

            uint8_t* mem; /// allocate memory
            return new( mem ) thread_record{
                    reinterpret_cast<hazard_pointer_guard*>(mem + sizeof(thread_record)),
                    hazard_pointers_per_thread,
                    reinterpret_cast<retired_ptr*>( mem + sizeof( thread_record ) + guard_array_size ),
                    0
            };
        }

        basic_smr::thread_record* basic_smr::alloc_thread_data() {
            thread_record* allocatedRecord;

            /// Before alloc new thread data, try to reuse
            for(allocatedRecord = m_threadList.load(std::memory_order::memory_order_acquire); allocatedRecord;
                allocatedRecord = allocatedRecord -> next_) {
                thread_record* null_record = nullptr;
                if(!allocatedRecord -> owner_rec_.compare_exchange_strong(null_record, allocatedRecord,
                                                                          std::memory_order::memory_order_relaxed,
                                                                          std::memory_order::memory_order_relaxed))
                    continue;
                allocatedRecord -> free_.store(false, std::memory_order::memory_order_release);
                return allocatedRecord;
            }


            allocatedRecord = create_thread_data();
            allocatedRecord -> owner_rec_.store(allocatedRecord, std::memory_order::memory_order_relaxed);

            thread_record* oldHead = m_threadList.load(std::memory_order::memory_order_relaxed);
            do {
                allocatedRecord -> next_ = oldHead;
            } while(!m_threadList.compare_exchange_weak(oldHead, allocatedRecord,
                                                        std::memory_order::memory_order_release,
                                                        std::memory_order::memory_order_acquire));

            return allocatedRecord;
        }



        void basic_smr::destroy_thread_data(details::basic_smr::thread_record* record) {
            record -> ~thread_record();
            /// free record
        }

        void basic_smr::free_thread_data(details::basic_smr::thread_record* threadRecord, bool callHelpScan) {
            assert( threadRecord != nullptr );
            // threadRecord -> hazards -> clear();
            threadRecord -> owner_rec_.store(nullptr, std::memory_order::memory_order_release);
        }




        /// \brief Safe memory reclamation wrapper
        template<typename TLSManager>
        class Generic_SMR: public basic_smr {
        public:
            static thread_data* getTls() {
                thread_data* data = TLSManager::getTLS();
                return data;
            }
            /// Attach current thread to HP
            static void attach_thread()
            {
                if ( !TLSManager::getTLS() )
                    TLSManager::setTLS(getInstance().alloc_thread_data());
            }

            /// Detach current thread from HP
            static void detach_thread()
            {
                thread_data* rec = TLSManager::getTLS();
                if ( rec ) {
                    TLSManager::setTLS(nullptr);
                    getInstance().free_thread_data(static_cast<thread_record*>( rec ), true );
                }
            }
        };

        using SMR = Generic_SMR<TLSManager>;
    }

    template<typename TLSManager>
    class hazard_pointer {
    private:
        using hp_implementation = details::Generic_SMR<TLSManager>;

    public:

        /// \brief Hazard Pointer allocation
        class Guard {
        public:
            Guard(): m_hpGuard{hp_implementation::getTls() -> hazards.alloc()} {}

            explicit Guard( std::nullptr_t ) noexcept: m_hpGuard( nullptr )
            {}

            Guard(const Guard& other) = delete;
            Guard& operator=(const Guard& other) = delete;

            Guard(Guard&& other) noexcept: m_hpGuard(other.m_hpGuard) {
                other.m_hpGuard = nullptr;
            }

            Guard& operator=(Guard&& other) noexcept {
                std::swap(m_hpGuard, other.m_hpGuard);
                return *this;
            }

            Guard& operator=(std::nullptr_t) {
                m_hpGuard = nullptr;
                return *this;
            }

            ~Guard() {
                unlink();
            }

            void unlink() {
                if(m_hpGuard) {
                    /// free
                    m_hpGuard = nullptr;
                }
            }

            template<typename T>
            T protect(const std::atomic<T>& nonGuarded) {
                T current = nonGuarded.load(std::memory_order::memory_order_relaxed);
                T ret;
                do {
                    ret = current;
                    assign(current);
                    current = nonGuarded.load(std::memory_order::memory_order_acquire);
                } while (current != ret);
                return current;
            }

            template<typename T>
            T* assign(T* p) {
                // assert(m_hpGuard);
                m_hpGuard -> set(p);
                hp_implementation::getTls() -> sync();
                return p;
            }

        private:
            details::hazard_pointer_guard* m_hpGuard;
        };

        template <size_t Count>
        class GuardArray
        {
        public:
            /// Rebind array for other size \p Count2
            template <size_t Count2>
            struct rebind {
                typedef GuardArray<Count2>  other;   ///< rebinding result
            };

            /// Array capacity
            static constexpr const size_t c_nCapacity = Count;

        public:
            /// Default ctor allocates \p Count hazard pointers
            GuardArray()
            {
                hp_implementation::tls()->hazards_.alloc( guards_ );
            }

            /// Move ctor is prohibited
            GuardArray( GuardArray&& ) = delete;

            /// Move assignment is prohibited
            GuardArray& operator=( GuardArray&& ) = delete;

            /// Copy ctor is prohibited
            GuardArray( GuardArray const& ) = delete;

            /// Copy assignment is prohibited
            GuardArray& operator=( GuardArray const& ) = delete;

            /// Frees allocated hazard pointers
            ~GuardArray()
            {
                hp_implementation::tls()->hazards_.free( guards_ );
            }

            /// Protects a pointer of type \p atomic<T*>
            /**
                Return the value of \p toGuard

                The function tries to load \p toGuard and to store it
                to the slot \p nIndex repeatedly until the guard's value equals \p toGuard
            */
            template <typename T>
            T protect( size_t nIndex, std::atomic<T> const& toGuard )
            {
                return protect(nIndex, toGuard, [](T p) { return p; });
            }

            /// Protects a pointer of type \p atomic<T*>
            /**
                Return the value of \p toGuard

                The function tries to load \p toGuard and to store it
                to the slot \p nIndex repeatedly until the guard's value equals \p toGuard

                The function is useful for intrusive containers when \p toGuard is a node pointer
                that should be converted to a pointer to the value type before guarding.
                The parameter \p f of type Func is a functor that makes this conversion:
                \code
                    struct functor {
                        value_type * operator()( T * p );
                    };
                \endcode
                Really, the result of <tt> f( toGuard.load()) </tt> is assigned to the hazard pointer.
            */
            template <typename T, class Func>
            T protect( size_t nIndex, std::atomic<T> const& toGuard, Func f )
            {
                assert( nIndex < capacity());

                T pRet;
                do {
                    assign( nIndex, f( pRet = toGuard.load(std::memory_order::memory_order_relaxed)));
                } while ( pRet != toGuard.load(std::memory_order::memory_order_acquire));

                return pRet;
            }

            /// Store \p to the slot \p nIndex
            /**
                The function equals to a simple assignment, no loop is performed.
            */
            template <typename T>
            T * assign( size_t nIndex, T * p )
            {
                assert( nIndex < capacity());

                guards_.set( nIndex, p );
                hp_implementation::tls()->sync();
                return p;
            }



            /// Copy guarded value from \p src guard to slot at index \p nIndex
            void copy( size_t nIndex, Guard const& src )
            {
                assign( nIndex, src.get_native());
            }

            /// Copy guarded value from slot \p nSrcIndex to the slot \p nDestIndex
            void copy( size_t nDestIndex, size_t nSrcIndex )
            {
                // assign( nDestIndex, get_native( nSrcIndex ));
            }

            /// Clear value of the slot \p nIndex
            void clear( size_t nIndex )
            {
                guards_.clear( nIndex );
            }

            /// Get current value of slot \p nIndex
            template <typename T>
            T * get( size_t nIndex ) const
            {
                assert( nIndex < capacity());
                //return guards_[nIndex]->template get_as<T>();
            }



            //@cond
            details::hazard_pointer_guard* release( size_t nIndex ) noexcept
            {
                return guards_.release( nIndex );
            }
            //@endcond

            /// Capacity of the guard array
            static constexpr size_t capacity()
            {
                return c_nCapacity;
            }

        private:
            //@cond
            details::guard_array<c_nCapacity> guards_;
            //@endcond
        };


    public:

        hazard_pointer(
                size_t nHazardPtrCount = 0,     ///< Hazard pointer count per thread
                size_t nMaxThreadCount = 0,     ///< Max count of simultaneous working thread in your application
                size_t nMaxRetiredPtrCount = 0, ///< Capacity of the array of retired objects for the thread
                details::scan_type nScanType = details::scan_type::inplace   ///< Scan type (see \p scan_type enum)
        ) {
            hp_implementation::construct(
                    nHazardPtrCount,
                    nMaxThreadCount,
                    nMaxRetiredPtrCount,
                    nScanType
            );
        }

        ~hazard_pointer() {
            hp_implementation::destruct( true );
        }

        template<typename T>
        static void retire(T* p, void(*remove_func)(void*)) {
            auto* tls = hp_implementation::getTls();
            if(!tls -> retired.push(details::retired_ptr(p, remove_func))){
                /// scan
            }
        }

    };

    using defaultHazardPointer = hazard_pointer<details::TLSManager>;

}
