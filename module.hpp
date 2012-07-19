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

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <fastcgi2/component.h>
#include <fastcgi2/handler.h>
#include <fastcgi2/logger.h>
#include <fastcgi2/request.h>

#include <cocaine/dealer/dealer.hpp>

namespace cocaine {
namespace dealer {

class fastcgi_module_t:
    virtual public fastcgi::Component, 
    virtual public fastcgi::Handler 
{
public:
	fastcgi_module_t(fastcgi::ComponentContext* context);
	virtual ~fastcgi_module_t();

private:
	fastcgi::Logger* log() const {
		return m_logger;
	}

    message_path_t make_path(const std::string& script_name) const;

    void update_policy_from_headers(message_policy_t& policy,
                                    fastcgi::Request& request);

	bool get_config_param(bool& param,
						  const std::string& path);

    template<typename T> bool get_config_param(T& param,
    										   const std::string& path)
    {
    	std::string component_path(context()->getComponentXPath());

    	try {
    		const fastcgi::Config* config = context()->getConfig();
	        std::string param_value(config->asString(component_path + path));
	        boost::trim(param_value);
	        param = boost::lexical_cast<T>(param_value);
	    }
	    catch (const std::exception& ex) {
	        if (m_logger) {
	            std::string log_str = "can't get %s config value, details: %s";
	            log()->debug(log_str.c_str(), path.c_str(), ex.what());
	        }

	        return false;
	    }

	    return true;
	}

	bool header_value(bool& value,
					  const std::string& header_name,
					  fastcgi::Request& request);

	template<typename T> bool header_value(T& value,
										   const std::string& header_name,
										   fastcgi::Request& request)
	{
    	std::string header_value = request.getHeader(header_name);
    	if (header_value.empty()) {
    		return false;
    	}

    	try {
    		value = boost::lexical_cast<T>(header_value);
    	}
    	catch (const std::exception& ex) {
    		if (m_logger) {
	            std::string log_str = "can't parse header: %s, value %s, details: %s";
	            log()->debug(log_str.c_str(),
	            			 header_name.c_str(),
	            			 header_value.c_str(),
	            			 ex.what());
	        }

	        return false;
    	}

    	return true;
    }

public:
	virtual void onLoad();
	virtual void onUnload();
	virtual void handleRequest(fastcgi::Request* request,
							   fastcgi::HandlerContext* context);

private:
	fastcgi::Logger*		m_logger;
    std::auto_ptr<dealer_t>	m_dealer;
    message_policy_t		m_default_policy;
};

} // namespace dealer
} // namespace cocaine

#endif

