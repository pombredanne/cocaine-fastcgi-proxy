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

#ifndef COCAINE_FASTCGI_MODULE_HPP
#define COCAINE_FASTCGI_MODULE_HPP

#include <map>
#include <string>

#include <fastcgi2/component.h>
#include <fastcgi2/handler.h>
#include <fastcgi2/logger.h>
#include <fastcgi2/request.h>

#include <cocaine/dealer/client.hpp>

namespace cocaine { namespace dealer {

class fastcgi_module_t:
    virtual public fastcgi::Component, 
    virtual public fastcgi::Handler 
{
public:
	fastcgi_module_t(fastcgi::ComponentContext * context);
	virtual ~fastcgi_module_t();

private:
	fastcgi::Logger* log() const {
		return m_logger;
	}

    message_path make_path(const std::string& script_name) const;

public:
	virtual void onLoad();
	virtual void onUnload();
	virtual void handleRequest(fastcgi::Request * request, fastcgi::HandlerContext * context);

private:
	fastcgi::Logger * m_logger;
    std::auto_ptr<client> m_client;
};

}}

#endif

