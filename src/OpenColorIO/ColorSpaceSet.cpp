// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <sstream>

#include <OpenColorIO/OpenColorIO.h>

#include "PrivateTypes.h"
#include "pystring/pystring.h"


OCIO_NAMESPACE_ENTER
{
    
class ColorSpaceSet::Impl
{
public:
    Impl() { }
    ~Impl() { }

    Impl(const Impl &) = delete;
    
    Impl & operator= (const Impl & rhs)
    {
        if(this!=&rhs)
        {
            clear();

            for(auto & cs: rhs.m_colorSpaces)
            {
                m_colorSpaces.push_back(cs->createEditableCopy());
            }
        }
        return *this;
    }

    bool operator== (const Impl & rhs)
    {
        if(this==&rhs) return true;

        if(m_colorSpaces.size()!=rhs.m_colorSpaces.size())
        {
            return false;
        }

        for(auto & cs : m_colorSpaces)
        {
            // NB: Only the names are compared.
            if(-1==rhs.getIndex(cs->getName()))
            {
                return false;
            }
        }

        return true;
    }

    int size() const 
    { 
        return static_cast<int>(m_colorSpaces.size()); 
    }

    ConstColorSpaceRcPtr get(int index) const 
    {
        if(index<0 || index>=size())
        {
            return ColorSpaceRcPtr();
        }

        return m_colorSpaces[index];
    }

    const char * getName(int index) const 
    {
        if(index<0 || index>=size())
        {
            return nullptr;
        }

        return m_colorSpaces[index]->getName();
    }

    ConstColorSpaceRcPtr getByName(const char * csName) const 
    {
        if(csName && *csName)
        {
            const std::string str = pystring::lower(csName);
            for(auto & cs: m_colorSpaces)
            {
                if(pystring::lower(cs->getName())==str)
                {
                    return cs;
                }
            }
        }
        return ColorSpaceRcPtr();
    }

    int getIndex(const char * csName) const 
    {
        if(csName && *csName)
        {
            const std::string str = pystring::lower(csName);
            for(size_t idx = 0; idx<m_colorSpaces.size(); ++idx)
            {
                if(pystring::lower(m_colorSpaces[idx]->getName())==str)
                {
                    return static_cast<int>(idx);
                }
            }
        }

        return -1;
    }

    void add(const ConstColorSpaceRcPtr & cs)
    {
        const std::string csName = pystring::lower(cs->getName());
        if(csName.empty())
        {
            throw Exception("Cannot add a color space with an empty name.");
        }

        for(auto & entry: m_colorSpaces)
        {
            if(pystring::lower(entry->getName())==csName)
            {
                // The color space replaces the existing one.
                entry = cs->createEditableCopy();
                return;
            }
        }

        m_colorSpaces.push_back(cs->createEditableCopy());
    }

    void add(const Impl & rhs)
    {
        for(auto & cs : rhs.m_colorSpaces)
        {
            add(cs);
        }
    }

    void remove(const char * csName)
    {
        const std::string name = pystring::lower(csName);
        if(name.empty()) return;

        for(auto itr = m_colorSpaces.begin(); itr != m_colorSpaces.end(); ++itr)
        {
            if(pystring::lower((*itr)->getName())==name)
            {
                m_colorSpaces.erase(itr);
                return;
            }
        }
    }

    void remove(const Impl & rhs)
    {
        for(auto & cs : rhs.m_colorSpaces)
        {
            remove(cs->getName());
        }
    }

    void clear()
    {
        m_colorSpaces.clear();
    }

private:
    typedef std::vector<ColorSpaceRcPtr> ColorSpaceVec;
    ColorSpaceVec m_colorSpaces;
};


///////////////////////////////////////////////////////////////////////////

ColorSpaceSetRcPtr ColorSpaceSet::Create()
{
    return ColorSpaceSetRcPtr(new ColorSpaceSet(), &deleter);
}

void ColorSpaceSet::deleter(ColorSpaceSet* c)
{
    delete c;
}


///////////////////////////////////////////////////////////////////////////



ColorSpaceSet::ColorSpaceSet()
    :   m_impl(new ColorSpaceSet::Impl)
{
}

ColorSpaceSet::~ColorSpaceSet()
{
    delete m_impl;
    m_impl = nullptr;
}
    
ColorSpaceSetRcPtr ColorSpaceSet::createEditableCopy() const
{
    ColorSpaceSetRcPtr css = ColorSpaceSet::Create();
    *css->m_impl = *m_impl;
    return css;
}

bool ColorSpaceSet::operator==(const ColorSpaceSet & css) const
{
    return *m_impl == *css.m_impl;
}

bool ColorSpaceSet::operator!=(const ColorSpaceSet & css) const
{
    return !( *m_impl == *css.m_impl );
}

int ColorSpaceSet::getNumColorSpaces() const
{
    return m_impl->size();    
}

const char * ColorSpaceSet::getColorSpaceNameByIndex(int index) const
{
    return m_impl->getName(index);
}

ConstColorSpaceRcPtr ColorSpaceSet::getColorSpaceByIndex(int index) const
{
    return m_impl->get(index);
}

ConstColorSpaceRcPtr ColorSpaceSet::getColorSpace(const char * name) const
{
    return m_impl->getByName(name);
}

int ColorSpaceSet::getIndexForColorSpace(const char * name) const
{
    return m_impl->getIndex(name);
}

void ColorSpaceSet::addColorSpace(const ConstColorSpaceRcPtr & cs)
{
    return m_impl->add(cs);
}

void ColorSpaceSet::addColorSpaces(const ConstColorSpaceSetRcPtr & css)
{
    return m_impl->add(*css->m_impl);
}

void ColorSpaceSet::removeColorSpace(const char * name)
{
    return m_impl->remove(name);
}

void ColorSpaceSet::removeColorSpaces(const ConstColorSpaceSetRcPtr & css)
{
    return m_impl->remove(*css->m_impl);
}

void ColorSpaceSet::clearColorSpaces()
{
    m_impl->clear();
}

ConstColorSpaceSetRcPtr operator||(const ConstColorSpaceSetRcPtr & lcss, 
                                   const ConstColorSpaceSetRcPtr & rcss)
{
    ColorSpaceSetRcPtr css = lcss->createEditableCopy();
    css->addColorSpaces(rcss);
    return css;    
}

ConstColorSpaceSetRcPtr operator&&(const ConstColorSpaceSetRcPtr & lcss, 
                                   const ConstColorSpaceSetRcPtr & rcss)
{
    ColorSpaceSetRcPtr css = ColorSpaceSet::Create();

    for(int idx=0; idx<rcss->getNumColorSpaces(); ++idx)
    {
        ConstColorSpaceRcPtr tmp = rcss->getColorSpaceByIndex(idx);
        if(-1!=lcss->getIndexForColorSpace(tmp->getName()))
        {
            css->addColorSpace(tmp);
        }
    }

    return css;
}

ConstColorSpaceSetRcPtr operator-(const ConstColorSpaceSetRcPtr & lcss, 
                                  const ConstColorSpaceSetRcPtr & rcss)
{
    ColorSpaceSetRcPtr css = ColorSpaceSet::Create();

    for(int idx=0; idx<lcss->getNumColorSpaces(); ++idx)
    {
        ConstColorSpaceRcPtr tmp = lcss->getColorSpaceByIndex(idx);

        if(-1==rcss->getIndexForColorSpace(tmp->getName()))
        {
            css->addColorSpace(tmp);
        }
    }

    return css;
}

}
OCIO_NAMESPACE_EXIT




///////////////////////////////////////////////////////////////////////////////

#ifdef OCIO_UNIT_TEST

namespace OCIO = OCIO_NAMESPACE;
#include "UnitTest.h"

OCIO_ADD_TEST(ColorSpaceSet, basic)
{
    OCIO::ConfigRcPtr config = OCIO::Config::Create();

    OCIO::ConstColorSpaceSetRcPtr css1;
    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(nullptr));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 0);

    // No category.

    OCIO::ColorSpaceRcPtr cs1 = OCIO::ColorSpace::Create();
    cs1->setName("cs1");
    OCIO_CHECK_ASSERT(!cs1->hasCategory("linear"));
    OCIO_CHECK_ASSERT(!cs1->hasCategory("rendering"));
    OCIO_CHECK_ASSERT(!cs1->hasCategory("log"));

    // Having categories to filter with.

    OCIO::ColorSpaceRcPtr cs2 = OCIO::ColorSpace::Create();
    cs2->setName("cs2");
    cs2->addCategory("linear");
    cs2->addCategory("rendering");
    OCIO_CHECK_ASSERT(cs2->hasCategory("linear"));
    OCIO_CHECK_ASSERT(cs2->hasCategory("rendering"));
    OCIO_CHECK_ASSERT(!cs2->hasCategory("log"));

    OCIO_CHECK_NO_THROW(cs2->addCategory("log"));
    OCIO_CHECK_ASSERT(cs2->hasCategory("log"));
    OCIO_CHECK_NO_THROW(cs2->removeCategory("log"));
    OCIO_CHECK_ASSERT(!cs2->hasCategory("log"));

    // Update config.

    OCIO_CHECK_NO_THROW(config->addColorSpace(cs1));
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs2));

    // Search some color spaces based on criteria.

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(nullptr));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 2);
    OCIO_CHECK_EQUAL(config->getNumColorSpaces(), 2);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(""));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 2);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("log"));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 0);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("linear"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs2"));

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("LinEar"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs2"));

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(" LinEar "));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs2"));
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceByIndex(0)->getName()), std::string("cs2"));

    // Test some faulty requests.

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("lin ear"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 0);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("[linear]"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 0);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("linear log"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 0);

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("linearlog"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 0);

    // Empty the config.

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("linear"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 1);

    OCIO_CHECK_NO_THROW(config->clearColorSpaces());
    OCIO_CHECK_EQUAL(config->getNumColorSpaces(), 0);
    // But existing sets are preserved.
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 1);

    OCIO::ConstColorSpaceSetRcPtr css2;
    OCIO_CHECK_NO_THROW(css2 = config->getColorSpaces(nullptr));
    OCIO_CHECK_EQUAL(css2->getNumColorSpaces(), 0);
}

OCIO_ADD_TEST(ColorSpaceSet, decoupled_sets)
{
    OCIO::ConfigRcPtr config = OCIO::Config::Create();

    OCIO::ColorSpaceRcPtr cs1 = OCIO::ColorSpace::Create();
    cs1->setName("cs1");
    OCIO_CHECK_NO_THROW(cs1->addCategory("linear"));
    OCIO_CHECK_ASSERT(cs1->hasCategory("linear"));
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs1));

    OCIO::ConstColorSpaceSetRcPtr css1;
    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(nullptr));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs1"));

    OCIO::ConstColorSpaceSetRcPtr css2;
    OCIO_CHECK_NO_THROW(css2 = config->getColorSpaces("linear"));
    OCIO_CHECK_EQUAL(css2->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css2->getColorSpaceNameByIndex(0)), std::string("cs1"));

    // Change the original color space.

    cs1->setName("new_cs1");

    // Check that color spaces in existing sets are not changed.
    OCIO_CHECK_EQUAL(std::string(config->getColorSpaceNameByIndex(0)), std::string("cs1"));

    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs1"));

    OCIO_CHECK_EQUAL(css2->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css2->getColorSpaceNameByIndex(0)), std::string("cs1"));

    // Change the color space from the config instance.

    OCIO_CHECK_ASSERT(!cs1->isData());
    config->clearColorSpaces();
    config->addColorSpace(cs1);
    cs1->setIsData(true);

    OCIO_CHECK_EQUAL(std::string(cs1->getName()), std::string("new_cs1"));
    OCIO_CHECK_ASSERT(cs1->isData());
    OCIO_CHECK_EQUAL(std::string(config->getColorSpaceNameByIndex(0)), std::string("new_cs1"));
    // NB: ColorSpace would need to be re-added to the config to reflect the change to isData.
    OCIO_CHECK_ASSERT(!config->getColorSpace("new_cs1")->isData());

    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_ASSERT(!css1->getColorSpace("cs1")->isData());

    OCIO_CHECK_EQUAL(css2->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css2->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_ASSERT(!css2->getColorSpace("cs1")->isData());
}

OCIO_ADD_TEST(ColorSpaceSet, order_validation)
{
    OCIO::ConfigRcPtr config = OCIO::Config::Create();

    OCIO::ConstColorSpaceSetRcPtr css1;
    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(nullptr));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 0);

    // Create some color spaces.

    OCIO::ColorSpaceRcPtr cs1 = OCIO::ColorSpace::Create();
    cs1->setName("cs1");
    cs1->addCategory("linear");
    cs1->addCategory("rendering");

    OCIO::ColorSpaceRcPtr cs2 = OCIO::ColorSpace::Create();
    cs2->setName("cs2");
    cs2->addCategory("rendering");
    cs2->addCategory("linear");

    OCIO::ColorSpaceRcPtr cs3 = OCIO::ColorSpace::Create();
    cs3->setName("cs3");
    cs3->addCategory("rendering");

    // Add the color spaces.

    OCIO_CHECK_NO_THROW(config->addColorSpace(cs1));
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs2));
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs3));

    // Check the color space order for the category "linear".

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("linear"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 2);

    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(1)), std::string("cs2"));

    // Check the color space order for the category "rendering".

    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces("rendering"));
    OCIO_REQUIRE_EQUAL(css1->getNumColorSpaces(), 3);

    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(1)), std::string("cs2"));
    OCIO_CHECK_EQUAL(std::string(css1->getColorSpaceNameByIndex(2)), std::string("cs3"));
}

OCIO_ADD_TEST(ColorSpaceSet, operations_on_set)
{
    OCIO::ConfigRcPtr config = OCIO::Config::Create();

    // No category.

    OCIO::ColorSpaceRcPtr cs1 = OCIO::ColorSpace::Create();
    cs1->setName("cs1");
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs1));

    // Having categories to filter with.

    OCIO::ColorSpaceRcPtr cs2 = OCIO::ColorSpace::Create();
    cs2->setName("cs2");
    cs2->addCategory("linear");
    cs2->addCategory("rendering");
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs2));

    OCIO::ColorSpaceRcPtr cs3 = OCIO::ColorSpace::Create();
    cs3->setName("cs3");
    cs3->addCategory("log");
    cs3->addCategory("rendering");
    OCIO_CHECK_NO_THROW(config->addColorSpace(cs3));


    // Recap. of the existing color spaces:
    // cs1  -> name="cs1" i.e. no category
    // cs2  -> name="cs2", categories=[rendering, linear]
    // cs3  -> name="cs3", categories=[rendering, log]


    OCIO::ConstColorSpaceSetRcPtr css1;
    OCIO_CHECK_NO_THROW(css1 = config->getColorSpaces(nullptr));
    OCIO_CHECK_EQUAL(css1->getNumColorSpaces(), 3);

    OCIO::ConstColorSpaceSetRcPtr css2;
    OCIO_CHECK_NO_THROW(css2 = config->getColorSpaces("linear"));
    OCIO_CHECK_EQUAL(css2->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css2->getColorSpaceNameByIndex(0)), std::string("cs2"));

    OCIO::ConstColorSpaceSetRcPtr css3;
    OCIO_CHECK_NO_THROW(css3 = config->getColorSpaces("log"));
    OCIO_CHECK_EQUAL(css3->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css3->getColorSpaceNameByIndex(0)), std::string("cs3"));


    // Recap. of the existing color space sets:
    // ccs1 -> {cs1, cs2, cs3}
    // ccs2 -> {cs2}
    // css3 -> {cs3}


    // Test the union.

    OCIO::ConstColorSpaceSetRcPtr css4 = css2 || css3;
    OCIO_CHECK_EQUAL(css4->getNumColorSpaces(), 2); // {cs2, cs3}

    css4 = css1 || css2;
    OCIO_CHECK_EQUAL(css4->getNumColorSpaces(), 3); // no duplication i.e. all color spaces

    // Test the intersection.

    css4 = css2 && css3;
    OCIO_CHECK_EQUAL(css4->getNumColorSpaces(), 0);

    css4 = css2 && css1;
    OCIO_REQUIRE_EQUAL(css4->getNumColorSpaces(), 1); // {cs2}
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(0)), std::string("cs2"));
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceByIndex(0)->getName()), std::string("cs2"));

    // Test the difference.

    css4 = css1 - css3;
    OCIO_REQUIRE_EQUAL(css4->getNumColorSpaces(), 2); // {cs1, cs2}
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(1)), std::string("cs2"));

    css4 = css1 - css2;
    OCIO_REQUIRE_EQUAL(css4->getNumColorSpaces(), 2); // {cs1, cs3}
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(0)), std::string("cs1"));
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(1)), std::string("cs3"));

    // Test with several embedded operations.

    css4 = css1 - (css2 || css3);
    OCIO_REQUIRE_EQUAL(css4->getNumColorSpaces(), 1); // {cs1}
    OCIO_CHECK_EQUAL(std::string(css4->getColorSpaceNameByIndex(0)), std::string("cs1"));

    OCIO::ColorSpaceSetRcPtr css5;
    OCIO_CHECK_NO_THROW(css5 = config->getColorSpaces("rendering"));
    OCIO_CHECK_EQUAL(css5->getNumColorSpaces(), 2); // {cs2, cs3}
    // Manipulate the result with few tests.
    OCIO_CHECK_NO_THROW(css5->addColorSpace(cs1));
    OCIO_CHECK_EQUAL(css5->getNumColorSpaces(), 3); // {cs1, cs2, cs3}
    OCIO_CHECK_NO_THROW(css5->removeColorSpace("cs2"));
    OCIO_CHECK_NO_THROW(css5->removeColorSpace("cs1"));
    OCIO_CHECK_EQUAL(css5->getNumColorSpaces(), 1);
    OCIO_CHECK_EQUAL(std::string(css5->getColorSpaceNameByIndex(0)), std::string("cs3"));
    OCIO_CHECK_NO_THROW(css5->clearColorSpaces());
    OCIO_CHECK_EQUAL(css5->getNumColorSpaces(), 0);

    OCIO_CHECK_NO_THROW(css5 = config->getColorSpaces("rendering"));
    OCIO_REQUIRE_EQUAL(css5->getNumColorSpaces(), 2); // {cs2, cs3}
    OCIO_CHECK_EQUAL(std::string(css5->getColorSpaceNameByIndex(0)), std::string("cs2"));
    OCIO_CHECK_EQUAL(std::string(css5->getColorSpaceNameByIndex(1)), std::string("cs3"));

    css4 = (css1  - css5)  // ( {cs1, cs2, cs3} - {cs2, cs3} ) --> {cs1}
           && 
           (css2 || css3); // ( {cs2} || {cs3} )               --> {cs2, cs3}

    OCIO_CHECK_EQUAL(css4->getNumColorSpaces(), 0);
}

#endif // OCIO_UNIT_TEST
