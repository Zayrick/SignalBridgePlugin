#include "hid/HidReportDescriptor.h"

#include <cstddef>

#include "hid/rdf/parser.hpp"

namespace signalbridge
{
namespace
{
class TopLevelCollectionParser : public hid::rdf::parser<hid::rdf::reinterpret_iterator>
{
public:
    explicit TopLevelCollectionParser(
        const std::vector<std::uint8_t>& descriptor)
    {
        parse_items(hid::rdf::descriptor_view(descriptor.data(), descriptor.size()));
    }

    const std::vector<HidTopLevelCollection>& Collections() const
    {
        return collections_;
    }

private:
    control parse_collection_begin(
        hid::rdf::main::collection_type,
        const hid::rdf::global_item_store& global_state,
        const items_view_type& main_section,
        unsigned tlc_number) override
    {
        if(collection_depth_ == 0)
        {
            for(const item_type& item : main_section)
            {
                if(!item.has_tag(hid::rdf::tag::USAGE))
                {
                    continue;
                }

                const hid::usage_t usage = get_usage(item, global_state);
                collections_.push_back({
                    usage.id(),
                    usage.page_id(),
                    static_cast<int>(tlc_number - 1),
                });
                break;
            }
        }

        ++collection_depth_;
        return control::CONTINUE;
    }

    control parse_collection_end(
        const hid::rdf::global_item_store&,
        const items_view_type&,
        unsigned) override
    {
        if(collection_depth_ > 0)
        {
            --collection_depth_;
        }
        return control::CONTINUE;
    }

    unsigned collection_depth_ = 0;
    std::vector<HidTopLevelCollection> collections_;
};
}

std::vector<HidTopLevelCollection> ParseHidTopLevelCollections(
    const std::vector<std::uint8_t>& descriptor)
{
    if(descriptor.empty())
    {
        return {};
    }

    try
    {
        const TopLevelCollectionParser parser(descriptor);
        return parser.Collections();
    }
    catch(...)
    {
        return {};
    }
}
}
