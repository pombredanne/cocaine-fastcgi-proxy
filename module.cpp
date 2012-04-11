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

#include "module.hpp"

using namespace cocaine::dealer;

typedef boost::tokenizer<
    boost::char_separator<char>
> tokenizer_type;

typedef std::map<
    std::string,
    std::vector<std::string>
> query_map_type;

fastcgi_module_t::fastcgi_module_t(fastcgi::ComponentContext * context) :
	fastcgi::Component(context),
	m_logger(NULL),
    m_client(NULL)
{ }

fastcgi_module_t::~fastcgi_module_t() {
    m_client.reset();
}

namespace {
    struct extract_argument {
        extract_argument(fastcgi::Request * request_, query_map_type& query_map_):
            request(request_),
            query_map(query_map_)
        { }

        template<class T>
        void operator()(const T& name) {
            request->getArg(name, query_map[name]);
        }

        fastcgi::Request * request;
        query_map_type& query_map;
    };
}

void fastcgi_module_t::handleRequest(fastcgi::Request * request, fastcgi::HandlerContext * context) {
	(void)context;
    
    try {
        if(request->getRequestMethod() != "POST") {
            throw fastcgi::HttpException(400);
        }

        // Parse the message path
        // ----------------------

        std::string name(request->getScriptName());
        
        boost::char_separator<char> separator("/");
        tokenizer_type tokenizer(name, separator);
        
        std::vector<std::string> tokens;

        std::copy(
            tokenizer.begin(),
            tokenizer.end(),
            std::back_inserter(tokens)
        );

        if(tokens.size() != 2) {
            throw fastcgi::HttpException(400);
        }

        // Prepare the request object
        // --------------------------
        
        std::vector<std::string> argument_names;
        query_map_type query_map;

        request->argNames(argument_names);

        std::for_each(
            argument_names.begin(),
            argument_names.end(),
            extract_argument(request, query_map)
        );

        // Send the request
        // ----------------

        message_path path(tokens[0], tokens[1]);
        message_policy policy;

        response future = m_client->send_message(query_map, path, policy);

        // Fetch the responses
        // -------------------

        data_container * chunk = NULL;

        request->setStatus(200);
        request->setContentType("text/plain");

        while(future.get(chunk)) {
            request->write(
                static_cast<const char*>(chunk->data()),
                chunk->size()
            );
        }
	} catch (const fastcgi::HttpException &e) {
		throw;
	} catch (...) {
		throw fastcgi::HttpException(400);
	}
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
