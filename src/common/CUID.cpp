
#include "../game/chars/CChar.h"
#include "../game/CWorld.h"
#include "CUID.h"


// -----------------------------
//	CUIDBase
// -----------------------------

CObjBase * CUIDBase::ObjFind(dword dwPrivateUID)   // static
{
    if ( IsResource(dwPrivateUID) || !IsValidUID(dwPrivateUID) )
        return nullptr;
    return g_World.FindUID( dwPrivateUID & UID_O_INDEX_MASK );
}

CItem * CUIDBase::ItemFind(dword dwPrivateUID)    // static
{
    // Does item still exist or has it been deleted?
    // IsItem() may be faster ?
    return dynamic_cast<CItem *>(ObjFind(dwPrivateUID));
}
CChar * CUIDBase::CharFind(dword dwPrivateUID)    // static
{
    // Does character still exists?
    return dynamic_cast<CChar *>(ObjFind(dwPrivateUID));
}

CObjBase * CUIDBase::ObjFind() const
{
    return ObjFind(m_dwInternalVal);
}
CItem * CUIDBase::ItemFind() const 
{
    return ItemFind(m_dwInternalVal);
}
CChar * CUIDBase::CharFind() const
{
    return CharFind(m_dwInternalVal);
}


bool CUIDBase::IsValidUID(dword dwPrivateUID) // static
{
	return ( dwPrivateUID && ( dwPrivateUID & UID_O_INDEX_MASK ) != UID_O_INDEX_MASK );
}

bool CUIDBase::IsResource(dword dwPrivateUID) // static
{
    return (dwPrivateUID & UID_F_RESOURCE);
}

bool CUIDBase::IsValidResource(dword dwPrivateUID) // static
{
    return (IsResource(dwPrivateUID) && IsValidUID(dwPrivateUID));
}

bool CUIDBase::IsItem(dword dwPrivateUID) 	// static
{
	if ( (dwPrivateUID & (UID_F_RESOURCE|UID_F_ITEM)) == UID_F_ITEM )    // It's NOT a resource, and it's an item
		return true;	// might be static in client ?
	return false;
}

bool CUIDBase::IsChar(dword dwPrivateUID) // static
{
	if ( ( dwPrivateUID & (UID_F_RESOURCE|UID_F_ITEM)) == 0 )    // It's NOT a resource, and it's not an item
		return IsValidUID(dwPrivateUID);
	return false;
}


bool CUIDBase::IsObjDisconnected() const	// Not in the game world for some reason.
{
	if ( ( m_dwInternalVal & (UID_F_RESOURCE|UID_O_DISCONNECT)) == UID_O_DISCONNECT )
		return true;
	return false;
}

bool CUIDBase::IsObjTopLevel() const	// on the ground in the world.
{
	if ( ( m_dwInternalVal & (UID_F_RESOURCE|UID_O_DISCONNECT)) == 0 )
		return true;	// might be static in client ?
	return false;
}

bool CUIDBase::IsItemEquipped() const
{
	if ( (m_dwInternalVal & (UID_F_RESOURCE|UID_F_ITEM|UID_O_DISCONNECT)) == (UID_F_ITEM|UID_O_EQUIPPED))
		return IsValidUID();
	return false ;
}

bool CUIDBase::IsItemInContainer() const
{
	if ( ( m_dwInternalVal & (UID_F_RESOURCE|UID_F_ITEM|UID_O_DISCONNECT)) == (UID_F_ITEM|UID_O_CONTAINED) )
		return IsValidUID();
	return false;
}

void CUIDBase::SetObjContainerFlags( dword dwFlags )
{
	m_dwInternalVal = ( m_dwInternalVal & (UID_O_INDEX_MASK|UID_F_ITEM) ) | dwFlags;
}

void CUIDBase::RemoveObjFlags( dword dwFlags )
{
    m_dwInternalVal &= ~dwFlags;
}

dword CUIDBase::GetObjUID() const
{
	return ( m_dwInternalVal & (UID_O_INDEX_MASK|UID_F_ITEM) );
}

void CUIDBase::SetObjUID( dword dwVal )
{
	// can be set to -1 by the client.
	m_dwInternalVal = ( dwVal & (UID_O_INDEX_MASK|UID_F_ITEM) ) | UID_O_DISCONNECT;
}
