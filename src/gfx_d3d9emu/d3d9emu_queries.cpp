// IDirect3DQuery9: event queries are backend fences, occlusion queries count over one backend query per pass they
// span, timestamps are backend timestamps in nanoseconds (so their frequency is 1 GHz and they are never disjoint).

#include "d3d9emu.h"

#include <cstring>

namespace d3d9emu
{
namespace
{
constexpr UINT64 kTimestampFrequency = 1000000000ull;

template <class T>
HRESULT WriteResult(void *data, DWORD size, T value)
{
    if (!data)
        return D3D_OK;
    if (size < sizeof(T))
        return D3DERR_INVALIDCALL;
    std::memcpy(data, &value, sizeof(T));
    return D3D_OK;
}
} // namespace

Query::Query(Device *device, D3DQUERYTYPE type) : m_device(device), m_type(type)
{
    m_device->AddRef();
}

Query::~Query()
{
    if (m_open)
    {
        SplitAtPassEnd();
        m_device->QueryClosed(this);
    }
    Reset();
}

IUnknown *Query::Parent()
{
    return m_device;
}

void Query::Reset()
{
    for (gb::Query *part : m_parts)
        gb::DestroyQuery(part);
    m_parts.clear();
    gb::DestroyFence(m_fence);
    m_fence = nullptr;
}

HRESULT Query::GetDevice(IDirect3DDevice9 **device)
{
    if (!device)
        return D3DERR_INVALIDCALL;
    m_device->AddRef();
    *device = m_device;
    return D3D_OK;
}

DWORD Query::GetDataSize()
{
    switch (m_type)
    {
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        return sizeof(UINT64);
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        return sizeof(BOOL);
    default:
        return sizeof(DWORD);
    }
}

void Query::SplitAtPassEnd()
{
    if (m_partOpen)
        gb::EndQuery(m_parts.back());
    m_partOpen = false;
}

void Query::ResumeInPass()
{
    gb::Query *part = gb::CreateQuery(gb::QueryType::Occlusion);
    m_parts.push_back(part);
    gb::BeginQuery(part);
    m_partOpen = true;
}

HRESULT Query::Issue(DWORD flags)
{
    Guard guard(DeviceLock());
    switch (m_type)
    {
    case D3DQUERYTYPE_EVENT:
        if (!(flags & D3DISSUE_END))
            return D3DERR_INVALIDCALL;
        Reset();
        m_fence = gb::InsertFence();
        m_issued = true;
        return D3D_OK;
    case D3DQUERYTYPE_OCCLUSION:
        if (flags & D3DISSUE_BEGIN)
        {
            if (m_open)
            {
                SplitAtPassEnd();
                m_device->QueryClosed(this);
            }
            Reset();
            m_issued = false;
            m_open = true;
            // Counting starts in the open pass, or in the next one to begin: nothing is drawn in between.
            m_device->QueryOpened(this);
            return D3D_OK;
        }
        if (flags & D3DISSUE_END)
        {
            if (m_open)
            {
                SplitAtPassEnd();
                m_device->QueryClosed(this);
                m_open = false;
            }
            else
                Reset();
            m_issued = true;
            return D3D_OK;
        }
        return D3DERR_INVALIDCALL;
    case D3DQUERYTYPE_TIMESTAMP:
        if (!(flags & D3DISSUE_END))
            return D3DERR_INVALIDCALL;
        if (m_parts.empty())
            m_parts.push_back(gb::CreateQuery(gb::QueryType::Timestamp));
        gb::EndQuery(m_parts.back());
        m_issued = true;
        return D3D_OK;
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        m_issued = (flags & D3DISSUE_END) != 0;
        return D3D_OK;
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        if (!(flags & D3DISSUE_END))
            return D3DERR_INVALIDCALL;
        m_issued = true;
        return D3D_OK;
    default:
        return D3DERR_INVALIDCALL;
    }
}

HRESULT Query::GetData(void *data, DWORD size, DWORD)
{
    Guard guard(DeviceLock());
    switch (m_type)
    {
    case D3DQUERYTYPE_EVENT:
        if (m_fence && !gb::FenceReached(m_fence))
            return S_FALSE;
        return WriteResult<BOOL>(data, size, TRUE);
    case D3DQUERYTYPE_OCCLUSION:
    {
        if (!m_issued)
            return S_FALSE;
        UINT64 total = 0;
        for (gb::Query *part : m_parts)
        {
            uint64_t samples;
            if (!gb::QueryResult(part, &samples))
                return S_FALSE;
            total += samples;
        }
        return WriteResult<DWORD>(data, size, DWORD(total > 0xFFFFFFFFull ? 0xFFFFFFFFull : total));
    }
    case D3DQUERYTYPE_TIMESTAMP:
    {
        uint64_t ns;
        if (!m_issued || !gb::QueryResult(m_parts.back(), &ns))
            return S_FALSE;
        return WriteResult<UINT64>(data, size, ns);
    }
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        if (!m_issued)
            return S_FALSE;
        return WriteResult<BOOL>(data, size, FALSE);
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        if (!m_issued)
            return S_FALSE;
        return WriteResult<UINT64>(data, size, kTimestampFrequency);
    default:
        return D3DERR_INVALIDCALL;
    }
}
} // namespace d3d9emu
