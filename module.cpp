//
// Copyright (C) 2011-2012 Andrey Sibiryov <me@kobology.ru>
//
// Licensed under the BSD 2-Clause License (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <boost/tokenizer.hpp>

#include <fastcgi2/config.h>
#include <fastcgi2/component_factory.h>
#include <fastcgi2/cookie.h>
#include <fastcgi2/except.h>
#include <fastcgi2/util.h>

#include <cocaine/dealer/response.hpp>
#include <cocaine/dealer/utils/error.hpp>

#include "module.hpp"

using namespace cocaine::dealer;

fastcgi_module_t::fastcgi_module_t(fastcgi::ComponentContext * context) :
	fastcgi::Component(context),
	m_logger(NULL),
    m_client(NULL)
{ }

fastcgi_module_t::~fastcgi_module_t() {
    m_client.reset();
}

namespace {
    typedef boost::tokenizer<
        boost::char_separator<char>
    > tokenizer_type;

    typedef std::map<
        std::string,
        std::vector<std::string>
    > vector_map_t;

    typedef std::map<
        std::string,
        std::string
    > string_map_t;

    struct request_wrapper_t {
        typedef std::vector<std::string> string_vector_t;

        request_wrapper_t(fastcgi::Request * request):
            secure(request->isSecure()),
            host(request->getHost()),
            method(request->getRequestMethod()),
            content_length(0)
        {
            string_vector_t argument_names;
            
            request->argNames(argument_names);

            for(string_vector_t::const_iterator it = argument_names.begin();
                it != argument_names.end();
                ++it)
            {
                request->getArg(*it, query[*it]);
            }

            string_vector_t header_names;

            request->headerNames(header_names);
            
            for(string_vector_t::const_iterator it = header_names.begin();
                it != header_names.end();
                ++it)
            {
                headers[*it] = request->getHeader(*it);
            }

            string_vector_t cookie_names;

            request->cookieNames(cookie_names);
            
            for(string_vector_t::const_iterator it = cookie_names.begin();
                it != cookie_names.end();
                ++it)
            {
                cookies[*it] = request->getCookie(*it);
            }

            if(method == "POST") {
                request->requestBody().toString(body);
                content_type = request->getContentType();
                content_length = request->getContentLength();
            }
        }

        bool secure;
        std::string host;
        std::string method;

        vector_map_t query;
        string_map_t headers, cookies;

        std::string body;
        std::string content_type;
        std::streamsize content_length;

        MSGPACK_DEFINE(
            secure, method, host, 
            query, headers, cookies, 
            body, content_type, content_length
        );
    };
}

void fastcgi_module_t::handleRequest(fastcgi::Request * request, fastcgi::HandlerContext * context) {
	(void)context;

    data_container chunk;
    boost::shared_ptr<response> future;
    
    try {
        future = m_client->send_message(
            request_wrapper_t(request),
            make_path(request->getScriptName()),
            message_policy()
        );

        request->setStatus(200);
        request->setContentType("text/plain");

        log()->debug("Fetching responses");
        
        while(future->get(&chunk)) {
            log()->info("Got a chunk of %zu bytes", chunk.size());
            
            request->write(
                static_cast<const char*>(chunk.data()),
                chunk.size()
            );
        }
    } catch(const error& e) { 
        log()->error("send failed: %s", e.what());
        throw fastcgi::HttpException(400);
	} catch(const fastcgi::HttpException &e) {
		throw;
	} catch(...) {
        log()->error("unexpected exception");
		throw fastcgi::HttpException(400);
    }
}

message_path fastcgi_module_t::make_path(const std::string& script_name) const {
    boost::char_separator<char> separator("/");
    tokenizer_type tokenizer(script_name, separator);
    
    std::vector<std::string> tokens;

    std::copy(
        tokenizer.begin(),
        tokenizer.end(),
        std::back_inserter(tokens)
    );

    if(tokens.size() != 2) {
        log()->error(
            "invalid message path, got %d path components",
            tokens.size()
        );

        throw fastcgi::HttpException(400);
    }

    return message_path(tokens[0], tokens[1]);
}

void fastcgi_module_t::onLoad() {
	assert(NULL == m_logger);

	const fastcgi::Config * config = context()->getConfig();
	std::string path(context()->getComponentXPath());

	m_logger = context()->findComponent<fastcgi::Logger>(config->asString(path + "/logger"));

	if(!m_logger) {
		throw std::logic_error("can't find logger");
	}

    std::string config_path(config->asString(path + "/client/configuration"));
    m_client.reset(new client(config_path));
}

void fastcgi_module_t::onUnload() {
    m_client.reset();
}

FCGIDAEMON_REGISTER_FACTORIES_BEGIN()
FCGIDAEMON_ADD_DEFAULT_FACTORY("cocaine-fastcgi", fastcgi_module_t);
FCGIDAEMON_REGISTER_FACTORIES_END()
