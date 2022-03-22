/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, the SerenityOS developers.
 * Copyright (c) 2021, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Debug.h>
#include <AK/URL.h>
#include <LibWeb/ImageDecoding.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/HTMLLinkElement.h>
#include <LibWeb/Loader/ResourceLoader.h>

namespace Web::HTML {

HTMLLinkElement::HTMLLinkElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

HTMLLinkElement::~HTMLLinkElement() = default;

void HTMLLinkElement::inserted()
{
    HTMLElement::inserted();

    if (m_relationship & Relationship::Stylesheet && !(m_relationship & Relationship::Alternate)) {
        auto url = document().parse_url(href());
        dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Loading import URL: {}", url);
        auto request = LoadRequest::create_for_url_on_page(url, document().page());
        // NOTE: Mark this element as delaying the document load event *before* calling set_resource()
        //       as it may trigger a synchronous resource_did_load() callback.
        m_document_load_event_delayer.emplace(document());
        set_resource(ResourceLoader::the().load_resource(Resource::Type::Generic, request));

        // NOTE: If we ended up not loading a resource for whatever reason, don't delay the load event.
        if (!resource())
            m_document_load_event_delayer.clear();
    }

    if (m_relationship & Relationship::Preload) {
        // FIXME: Respect the "as" attribute.
        LoadRequest request;
        request.set_url(document().parse_url(attribute(HTML::AttributeNames::href)));
        m_preload_resource = ResourceLoader::the().load_resource(Resource::Type::Generic, request);
    } else if (m_relationship & Relationship::DNSPrefetch) {
        ResourceLoader::the().prefetch_dns(document().parse_url(attribute(HTML::AttributeNames::href)));
    } else if (m_relationship & Relationship::Preconnect) {
        ResourceLoader::the().preconnect(document().parse_url(attribute(HTML::AttributeNames::href)));
    } else if (m_relationship & Relationship::Icon) {
        auto favicon_url = document().parse_url(href());

        // Specs:
        // -https://html.spec.whatwg.org/multipage/links.html#linkTypes
        // - https://html.spec.whatwg.org/multipage/links.html#rel-icon
        //
        // There is already some favicon implementation at Userland\Libraries\LibWeb\Loader\FrameLoader.cpp FrameLoader::load for `AK::URL favicon_url;`
        // But with link tags, multiple favicons can be defined and which one is active depends on certain spec rules.
        ResourceLoader::the().load(
            favicon_url,
            [this, favicon_url](auto data, auto&, auto) {
                dbgln_if(SPAM_DEBUG, "Favicon downloaded, {} bytes from {}", data.size(), favicon_url);
                if (data.is_empty())
                    return;
                RefPtr<Gfx::Bitmap> favicon_bitmap;
                auto decoded_image = Web::image_decoder_client().decode_image(data);
                if (!decoded_image.has_value() || decoded_image->frames.is_empty()) {
                    dbgln("Could not decode favicon {}", favicon_url);
                } else {
                    favicon_bitmap = decoded_image->frames[0].bitmap;
                    dbgln_if(IMAGE_DECODER_DEBUG, "Decoded favicon, {}", favicon_bitmap->size());
                }
                if (auto* page = document().browsing_context()->page()) {
                    //page->client().page_did_change_favicon(favicon_bitmap);
                }
            },
            [](auto& error, auto) {
                dbgln("Failed to load favicon: {}", error);
                VERIFY_NOT_REACHED();
            });
    }
}

void HTMLLinkElement::parse_attribute(const FlyString& name, const String& value)
{
    if (name == HTML::AttributeNames::rel) {
        m_relationship = 0;
        auto parts = value.split_view(' ');
        for (auto& part : parts) {
            if (part == "stylesheet"sv)
                m_relationship |= Relationship::Stylesheet;
            else if (part == "alternate"sv)
                m_relationship |= Relationship::Alternate;
            else if (part == "preload"sv)
                m_relationship |= Relationship::Preload;
            else if (part == "dns-prefetch"sv)
                m_relationship |= Relationship::DNSPrefetch;
            else if (part == "preconnect"sv)
                m_relationship |= Relationship::Preconnect;
            else if (part == "icon"sv || part == "shortcut icon"sv)
                m_relationship |= Relationship::Icon;
        }
    }
}

void HTMLLinkElement::resource_did_fail()
{
    dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Resource did fail. URL: {}", resource()->url());

    m_document_load_event_delayer.clear();
}

void HTMLLinkElement::resource_did_load()
{
    VERIFY(resource());

    m_document_load_event_delayer.clear();

    if (!resource()->has_encoded_data()) {
        dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Resource did load, no encoded data. URL: {}", resource()->url());
    } else {
        dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Resource did load, has encoded data. URL: {}", resource()->url());

        if (resource()->mime_type() != "text/css"sv) {
            dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Resource did load, but MIME type was {} instead of text/css. URL: {}", resource()->mime_type(), resource()->url());
            return;
        }
    }

    auto sheet = parse_css(CSS::ParsingContext(document(), resource()->url()), resource()->encoded_data());
    if (!sheet) {
        dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Failed to parse stylesheet: {}", resource()->url());
        return;
    }

    sheet->set_owner_node(this);
    document().style_sheets().add_sheet(sheet.release_nonnull());
}

}
