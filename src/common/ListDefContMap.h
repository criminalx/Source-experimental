/**
* @file ListDefContMap.h
*
*/

#ifndef _INC_LISTDEFCONTMAP_H
#define _INC_LISTDEFCONTMAP_H

#include <list>
#include <set>
#include "common.h"
#include "sphere_library/CSString.h"


class CTextConsole;
class CScript;

class CListDefContElem
{
private:
	CSString m_Key;	// reference to map key

public:
	static const char *m_sClassName;

	explicit CListDefContElem(lpctstr pszKey);
	virtual ~CListDefContElem() = default;

private:
	CListDefContElem(const CListDefContElem& copy);
	CListDefContElem& operator=(const CListDefContElem& other);

public:
    inline lpctstr GetKey() const {
        return m_Key.GetPtr();
    }
	void SetKey( lpctstr pszKey );

	virtual lpctstr GetValStr() const = 0;
	virtual int64 GetValNum() const = 0;
	virtual CListDefContElem * CopySelf() const = 0;
};


class CListDefContNum: public CListDefContElem
{
private:
	int64 m_iVal;

public:
	static const char *m_sClassName;

	explicit CListDefContNum(lpctstr pszKey);
	CListDefContNum(lpctstr pszKey, int64 iVal);
	~CListDefContNum() = default;

private:
	CListDefContNum(const CListDefContNum& copy);
	CListDefContNum& operator=(const CListDefContNum& other);

public:
    inline int64 GetValNum() const {
        return m_iVal;
    }
    inline void SetValNum(int64 iVal) {
        m_iVal = iVal;
    }
	lpctstr GetValStr() const;

	bool r_LoadVal( CScript & s );
	bool r_WriteVal( lpctstr pKey, CSString & sVal, CTextConsole * pSrc );

	virtual CListDefContElem * CopySelf() const;
};


class CListDefContStr: public CListDefContElem
{
private:
	CSString m_sVal;	// the assigned value. (What if numeric?)

public:
	static const char *m_sClassName;

	CListDefContStr(lpctstr pszKey, lpctstr pszVal);
	explicit CListDefContStr(lpctstr pszKey);
	~CListDefContStr() = default;

private:
	CListDefContStr(const CListDefContStr& copy);
	CListDefContStr& operator=(const CListDefContStr& other);

public:
    inline lpctstr GetValStr() const {
        return m_sVal.GetPtr(); 
    }
	void SetValStr( lpctstr pszVal );
	int64 GetValNum() const;

	bool r_LoadVal( CScript & s );
	bool r_WriteVal( lpctstr pKey, CSString & sVal, CTextConsole * pSrc );

	virtual CListDefContElem * CopySelf() const;
};


class CListDefCont
{
private:
	CSString m_Key;	// reference to map key

	typedef std::list<CListDefContElem *> DefList;

protected:
	DefList	m_listElements;

	inline CListDefContElem* ElementAt(size_t nIndex) const;
	inline void DeleteAtIterator(DefList::iterator it);
	inline DefList::iterator _GetAt(size_t nIndex);

public:
	static const char *m_sClassName;

	explicit CListDefCont(lpctstr pszKey);
	~CListDefCont() = default;

private:
	CListDefCont(const CListDefCont& copy);
	CListDefCont& operator=(const CListDefCont& other);

public:
    inline lpctstr GetKey() const {
        return m_Key.GetPtr();
    }
	void SetKey( lpctstr pszKey );

	CListDefContElem* GetAt(size_t nIndex) const;
	bool SetNumAt(size_t nIndex, int64 iVal);
	bool SetStrAt(size_t nIndex, lpctstr pszVal);
	size_t GetCount() const;

	lpctstr GetValStr(size_t nIndex) const;
	int64 GetValNum(size_t nIndex) const;

	int FindValNum( int64 iVal, size_t nStartIndex = 0 ) const;
	int FindValStr( lpctstr pVal, size_t nStartIndex = 0 ) const;

	bool AddElementNum(int64 iVal);
	bool AddElementStr(lpctstr pszKey);

	bool RemoveElement(size_t nIndex);
	void RemoveAll();
	void Sort(bool bDesc = false, bool bCase = false);

	bool InsertElementNum(size_t nIndex, int64 iVal);
	bool InsertElementStr(size_t nIndex, lpctstr pszKey);

	CListDefCont * CopySelf();
	void PrintElements(CSString& strElements) const;
	void DumpElements( CTextConsole * pSrc, lpctstr pszPrefix = nullptr ) const;
	void r_WriteSave( CScript& s );
	bool r_LoadVal( CScript& s );
	bool r_LoadVal( lpctstr pszArg );
};


class CListDefMap
{
private:
	struct ltstr
	{
		bool operator()(CListDefCont * s1, CListDefCont * s2) const;
	};

	typedef std::set<CListDefCont *, ltstr> DefSet;
	typedef std::pair<DefSet::iterator, bool> DefPairResult;

private:
	DefSet m_Container;

public:
	static const char *m_sClassName;
	CListDefMap & operator = ( const CListDefMap & array );
	CListDefMap() { };
	~CListDefMap();

private:
	CListDefMap(const CListDefMap& copy);

private:
	CListDefCont * GetAtKey( lpctstr at );
	inline void DeleteAt( size_t at );
	inline void DeleteAtKey( lpctstr at );
	inline void DeleteAtIterator( DefSet::iterator it );

public:
	void Copy( const CListDefMap * pArray );
	void Empty();
	size_t GetCount() const;

	lpctstr FindValNum( int64 iVal ) const;
	lpctstr FindValStr( lpctstr pVal ) const;

	CListDefCont * GetAt( size_t at );
	CListDefCont * GetKey( lpctstr pszKey ) const;

	CListDefCont* AddList(lpctstr pszKey);

	void DumpKeys( CTextConsole * pSrc, lpctstr pszPrefix = nullptr );
	void ClearKeys(lpctstr mask = nullptr);
	void DeleteKey( lpctstr key );

	bool r_LoadVal( lpctstr pszKey, CScript & s );
	bool r_Write( CTextConsole *pSrc, lpctstr pszString, CSString& strVal );
	void r_WriteSave( CScript& s );
};

#endif // _INC_LISTDEFCONTMAP_H
