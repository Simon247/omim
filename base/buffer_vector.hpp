#pragma once
#include "base/assert.hpp"
#include "base/stl_iterator.hpp"
#include "base/swap.hpp"

#include "std/algorithm.hpp"
#include "std/cstring.hpp"       // for memcpy
#include "std/type_traits.hpp"
#include "std/utility.hpp"
#include "std/vector.hpp"


template <class T, size_t N> class buffer_vector
{
private:
  enum { USE_DYNAMIC = N + 1 };
  T m_static[N];
  size_t m_size;
  vector<T> m_dynamic;

  inline bool IsDynamic() const { return m_size == USE_DYNAMIC; }

  /// @todo clang on linux doesn't have is_trivially_copyable.
#ifndef OMIM_OS_LINUX
  template <class U = T>
  typename enable_if<is_trivially_copyable<U>::value, void>::type
  MoveStatic(buffer_vector<T, N> & rhs)
  {
    memcpy(m_static, rhs.m_static, rhs.m_size*sizeof(T));
  }
  template <class U = T>
  typename enable_if<!is_trivially_copyable<U>::value, void>::type
  MoveStatic(buffer_vector<T, N> & rhs)
  {
    for (size_t i = 0; i < rhs.m_size; ++i)
      Swap(m_static[i], rhs.m_static[i]);
  }
#else
  template <class U = T>
  typename enable_if<is_pod<U>::value, void>::type
  MoveStatic(buffer_vector<T, N> & rhs)
  {
    memcpy(m_static, rhs.m_static, rhs.m_size*sizeof(T));
  }
  template <class U = T>
  typename enable_if<!is_pod<U>::value, void>::type
  MoveStatic(buffer_vector<T, N> & rhs)
  {
    for (size_t i = 0; i < rhs.m_size; ++i)
      Swap(m_static[i], rhs.m_static[i]);
  }
#endif

public:
  typedef T value_type;
  typedef T const & const_reference;
  typedef T & reference;
  typedef size_t size_type;
  typedef T const * const_iterator;
  typedef T * iterator;

  buffer_vector() : m_size(0) {}
  explicit buffer_vector(size_t n, T c = T()) : m_size(0)
  {
    resize(n, c);
  }

  explicit buffer_vector(initializer_list<T> const & initList) : m_size(0)
  {
    assign(initList.begin(), initList.end());
  }

  template <typename IterT>
  buffer_vector(IterT beg, IterT end) : m_size(0)
  {
    assign(beg, end);
  }

  buffer_vector(buffer_vector<T, N> const &) = default;
  buffer_vector & operator=(buffer_vector<T, N> const &) = default;

  buffer_vector(buffer_vector<T, N> && rhs)
    : m_size(rhs.m_size), m_dynamic(move(rhs.m_dynamic))
  {
    if (!IsDynamic())
      MoveStatic(rhs);

    rhs.m_size = 0;
  }

  buffer_vector & operator=(buffer_vector<T, N> && rhs)
  {
    m_size = rhs.m_size;
    m_dynamic = move(rhs.m_dynamic);

    if (!IsDynamic())
      MoveStatic(rhs);

    rhs.m_size = 0;
    return *this;
  }

  template <size_t M>
  void append(buffer_vector<value_type, M> const & v)
  {
    append(v.begin(), v.end());
  }

  template <typename IterT>
  void append(IterT beg, IterT end)
  {
    if (IsDynamic())
      m_dynamic.insert(m_dynamic.end(), beg, end);
    else
    {
      while (beg != end)
      {
        if (m_size == N)
        {
          m_dynamic.reserve(N * 2);
          SwitchToDynamic();
          while (beg != end)
            m_dynamic.push_back(*beg++);
          break;
        }
        m_static[m_size++] = *beg++;
      }
    }
  }

  template <typename IterT>
  void assign(IterT beg, IterT end)
  {
    if (IsDynamic())
      m_dynamic.assign(beg, end);
    else
    {
      m_size = 0;
      append(beg, end);
    }
  }

  void reserve(size_t n)
  {
    if (IsDynamic() || n > N)
      m_dynamic.reserve(n);
  }

  void resize_no_init(size_t n)
  {
    if (IsDynamic())
      m_dynamic.resize(n);
    else
    {
      if (n <= N)
        m_size = n;
      else
      {
        m_dynamic.reserve(n);
        SwitchToDynamic();
        m_dynamic.resize(n);
        ASSERT_EQUAL(m_dynamic.size(), n, ());
      }
    }
  }

  void resize(size_t n, T c = T())
  {
    if (IsDynamic())
      m_dynamic.resize(n, c);
    else
    {
      if (n <= N)
      {
        for (size_t i = m_size; i < n; ++i)
          m_static[i] = c;
        m_size = n;
      }
      else
      {
        m_dynamic.reserve(n);
        size_t const oldSize = m_size;
        SwitchToDynamic();
        m_dynamic.insert(m_dynamic.end(), n - oldSize, c);
        ASSERT_EQUAL(m_dynamic.size(), n, ());
      }
    }
  }

  void clear()
  {
    if (IsDynamic())
    {
      m_dynamic.clear();
    }
    else
    {
      // here we have to call destructors of objects inside
      for (size_t i = 0; i < m_size; ++i)
        m_static[i] = T();
      m_size = 0;
    }
  }

  /// @todo Here is some inconsistencies:
  /// - "data" method should return 0 if vector is empty;\n
  /// - potential memory overrun if m_dynamic is empty;\n
  /// The best way to fix this is to reset m_size from USE_DYNAMIC to 0 when vector becomes empty.
  /// But now I will just add some assertions to test memory overrun.
  //@{
  T const * data() const
  {
    if (IsDynamic())
    {
      ASSERT ( !m_dynamic.empty(), () );
      return &m_dynamic[0];
    }
    else
      return &m_static[0];
  }

  T * data()
  {
    if (IsDynamic())
    {
      ASSERT ( !m_dynamic.empty(), () );
      return &m_dynamic[0];
    }
    else
      return &m_static[0];
  }
  //@}

  T const * begin() const { return data(); }
  T       * begin()       { return data(); }
  T const * end() const { return data() + size(); }
  T       * end()       { return data() + size(); }
  //@}

  bool empty() const { return (IsDynamic() ? m_dynamic.empty() : m_size == 0); }
  size_t size() const { return (IsDynamic() ? m_dynamic.size() : m_size); }

  T const & front() const
  {
    ASSERT(!empty(), ());
    return *begin();
  }
  T & front()
  {
    ASSERT(!empty(), ());
    return *begin();
  }
  T const & back() const
  {
    ASSERT(!empty(), ());
    return *(end() - 1);
  }
  T & back()
  {
    ASSERT(!empty(), ());
    return *(end() - 1);
  }

  T const & operator[](size_t i) const
  {
    ASSERT_LESS(i, size(), ());
    return *(begin() + i);
  }
  T & operator[](size_t i)
  {
    ASSERT_LESS(i, size(), ());
    return *(begin() + i);
  }

  void swap(buffer_vector<T, N> & rhs)
  {
    m_dynamic.swap(rhs.m_dynamic);
    Swap(m_size, rhs.m_size);
    for (size_t i = 0; i < N; ++i)
      Swap(m_static[i], rhs.m_static[i]);
  }

  void push_back(T const & t)
  {
    if (IsDynamic())
      m_dynamic.push_back(t);
    else
    {
      if (m_size < N)
        m_static[m_size++] = t;
      else
      {
        ASSERT_EQUAL(m_size, N, ());
        SwitchToDynamic();
        m_dynamic.push_back(t);
        ASSERT_EQUAL(m_dynamic.size(), N + 1, ());
      }
    }
  }

  void push_back(T && t)
  {
    if (IsDynamic())
      m_dynamic.push_back(move(t));
    else
    {
      if (m_size < N)
        Swap(m_static[m_size++], t);
      else
      {
        ASSERT_EQUAL(m_size, N, ());
        SwitchToDynamic();
        m_dynamic.push_back(move(t));
        ASSERT_EQUAL(m_dynamic.size(), N + 1, ());
      }
    }
  }

  void pop_back()
  {
    if (IsDynamic())
      m_dynamic.pop_back();
    else
    {
      ASSERT_GREATER(m_size, 0, ());
      --m_size;
    }
  }

  template <class... Args>
  void emplace_back(Args &&... args)
  {
    if (IsDynamic())
      m_dynamic.emplace_back(args...);
    else
    {
      if (m_size < N)
      {
        value_type v(args...);
        Swap(v, m_static[m_size++]);
      }
      else
      {
        ASSERT_EQUAL(m_size, N, ());
        SwitchToDynamic();
        m_dynamic.emplace_back(args...);
        ASSERT_EQUAL(m_dynamic.size(), N + 1, ());
      }
    }
  }

  template <typename IterT> void insert(const_iterator where, IterT beg, IterT end)
  {
    ptrdiff_t const pos = where - data();
    ASSERT_GREATER_OR_EQUAL(pos, 0, ());
    ASSERT_LESS_OR_EQUAL(pos, static_cast<ptrdiff_t>(size()), ());

    if (IsDynamic())
      m_dynamic.insert(m_dynamic.begin() + pos, beg, end);
    else
    {
      size_t const n = end - beg;
      if (m_size + n <= N)
      {
        if (pos != m_size)
          for (ptrdiff_t i = m_size - 1; i >= pos; --i)
            Swap(m_static[i], m_static[i + n]);

        m_size += n;
        T * writableWhere = &m_static[0] + pos;
        ASSERT_EQUAL(where, writableWhere, ());
        while (beg != end)
          *(writableWhere++) = *(beg++);
      }
      else
      {
        m_dynamic.reserve(m_size + n);
        SwitchToDynamic();
        m_dynamic.insert(m_dynamic.begin() + pos, beg, end);
      }
    }
  }

  inline void insert(const_iterator where, value_type const & value)
  {
    insert(where, &value, &value + 1);
  }

  template <class TFn>
  void erase_if(TFn fn)
  {
    iterator b = begin();
    iterator e = end();
    iterator i = remove_if(b, e, fn);
    if (i != e)
      resize(distance(b, i));
  }

private:
  void SwitchToDynamic()
  {
    ASSERT_NOT_EQUAL(m_size, static_cast<size_t>(USE_DYNAMIC), ());
    ASSERT_EQUAL(m_dynamic.size(), 0, ());
    m_dynamic.insert(m_dynamic.end(), m_size, T());
    for (size_t i = 0; i < m_size; ++i)
      Swap(m_static[i], m_dynamic[i]);
    m_size = USE_DYNAMIC;
  }
};

template <class T, size_t N>
void swap(buffer_vector<T, N> & r1, buffer_vector<T, N> & r2)
{
  r1.swap(r2);
}

template <typename T, size_t N>
inline string DebugPrint(buffer_vector<T, N> const & v)
{
  return ::my::impl::DebugPrintSequence(v.data(), v.data() + v.size());
}

template <typename T, size_t N1, size_t N2>
inline bool operator==(buffer_vector<T, N1> const & v1, buffer_vector<T, N2> const & v2)
{
  return (v1.size() == v2.size() && std::equal(v1.begin(), v1.end(), v2.begin()));
}

template <typename T, size_t N1, size_t N2>
inline bool operator!=(buffer_vector<T, N1> const & v1, buffer_vector<T, N2> const & v2)
{
  return !(v1 == v2);
}

template <typename T, size_t N1, size_t N2>
inline bool operator<(buffer_vector<T, N1> const & v1, buffer_vector<T, N2> const & v2)
{
  return lexicographical_compare(v1.begin(), v1.end(), v2.begin(), v2.end());
}
