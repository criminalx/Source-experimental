/**
* @file CRandGroupDef.h
*
*/

#ifndef _INC_CRANDGROUPDEF_H
#define _INC_CRANDGROUPDEF_H

#include "../CResourceLink.h"
#include "../CResourceQty.h"


/**
* @class   CRandGroupDef
*
* @brief   A create struct random group definition.
*          RES_SPAWN or RES_REGIONTYPE
*          [SPAWN n] [REGIONTYPE x]
*
*/
class CRandGroupDef : public CResourceLink
{
private:
    static lpctstr const sm_szLoadKeys[];
    int m_iTotalWeight;
    CResourceQtyArray m_Members;
private:
    int CalcTotalWeight();
public:
    static const char *m_sClassName;
    explicit CRandGroupDef( CResourceID rid ) : CResourceLink( rid )
    {
        m_iTotalWeight = 0;
    }
    virtual ~CRandGroupDef() = default;

    CSString	m_sCategory;        // Axis Category
    CSString	m_sSubsection;      // Axis SubSection
    CSString	m_sDescription;     // Axis Description

private:
    CRandGroupDef(const CRandGroupDef& copy);
    CRandGroupDef& operator=(const CRandGroupDef& other);

public:
    virtual bool r_LoadVal( CScript & s ) override;
    virtual bool r_WriteVal( lpctstr pKey, CSString &sVal, CTextConsole * pSrc = nullptr ) override;
    size_t GetRandMemberIndex( CChar * pCharSrc = nullptr, bool bTrigger = true ) const;
    CResourceQty GetMember( size_t i ) const
    {
        return m_Members[i];
    }
    CResourceID GetMemberID( size_t i ) const
    {
        return m_Members[i].GetResourceID();
    }
    size_t BadMemberIndex() const
    {
        return m_Members.BadIndex();
    }
};


#endif // _INC_CRANDGROUPDEF_H
