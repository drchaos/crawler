#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <iterator>
#include <streambuf>
#include <list>
#include <future>
#include <chrono>
#include "Document.h"
#include "Node.h"
#define BOOST_NETWORK_ENABLE_HTTPS
#include <boost/network/protocol/http/client.hpp>
#include <boost/network/uri.hpp>
#include <boost/network/uri/builder.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
namespace alg = boost::algorithm;
namespace net = boost::network;

void saveFile(const fs::path& p, const std::string& body)
{
    fs::create_directories(p.parent_path());
    
    std::ofstream ofs(p.c_str());
    ofs << body;
}

void loadFile(const fs::path& p, std::string& body)
{
    std::ifstream ifs(p.c_str());
    
    ifs.seekg(0, std::ios::end);   
    body.reserve(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    
    body.assign((std::istreambuf_iterator<char>(ifs)),
                std::istreambuf_iterator<char>());
}

bool getBodyAndSave(const boost::network::uri::uri& uri, const fs::path& p, std::string& body_)
{
    using namespace boost::network;
    using namespace boost::network::http;
    
    if(!fs::exists(p))
    {
        client::request request_(uri);
        client client_;
        client::response response_ = client_.head(request_);
        auto status_ = status(response_);

        if(status_ == 200)
        {
            auto rCType = headers(response_)["Content-Type"];
            if (rCType)
            {
                const auto ctype = rCType.front().second;
                bool isHtml = alg::starts_with(ctype , "text/html");
                if (   isHtml 
                    || alg::starts_with(ctype , "text/css")
                    || alg::starts_with(ctype , "application/javascript")
                )
                {
                    client::response response_ = client_.get(request_);
                    body_ = body(response_);
                    saveFile(p, body_);
		    std::cout << uri.string() << " -> " << p << std::endl;
                    return isHtml;
                }
                else
                {
                    std::cout << uri.string() << " - document is ignored" << std::endl;
                }
            }
            else
            {
    	       std::cout << uri.string() << " - document has unknown type" << std::endl;
            }
        }
        else
        {
    	    std::cout << uri.string() << " - error response. Status : " << status_  << std::endl;
        }
    }
    else
    {
        loadFile(p, body_);
        std::cout << uri.string() <<  " <- loaded from file" << std::endl;
        return true;
    }
    return false;
}

boost::network::uri::uri removeFragmentPart(const boost::network::uri::uri& u)
{
    using namespace boost::network;
    
    if(!u.fragment().empty())
    {
        uri::uri r;
        uri::builder b(r);
        b.set_scheme(u.scheme());
        b.set_path(hierarchical_part(u));
        b.set_query(u.query());
        
        return r;
    }
    
    return u;
}

boost::network::uri::uri getBasePart(const boost::network::uri::uri& u)
{
    using namespace boost::network;
    
    uri::uri baseUrl;
    uri::builder b(baseUrl);
    b.set_scheme(u.scheme());
    b.set_host(u.host());
    if(!u.port().empty()) b.set_port(u.port());
    
    return baseUrl;
}

std::set<boost::network::uri::uri> downloadAndParse(const boost::network::uri::uri& url, const fs::path& basePath)
{
    using namespace boost::network;
    using namespace boost::network::http;
    
    std::set<boost::network::uri::uri> res;
    std::string query = url.query();
    auto ext = fs::path(url.path()).extension();
    fs::path file2Save;
    if(ext == "html" || ext == "css" || ext == "js")
    {
        file2Save = basePath / (url.path() + (query.empty() ? std::string() : '?' + query));
    }
    else
    {
        file2Save = basePath / (url.path() + (query.empty() ? std::string() : '?' + query)) / "index.html";
    }
    
    std::string body_;
    // is Html in body?
    if(getBodyAndSave(url, file2Save, body_))
    {
        CDocument doc;
        doc.parse(body_.c_str());
        
        uri::uri baseUrl;
        
        CSelection base_sel = doc.find("head base[href]");
        if(base_sel.nodeNum() > 0)
        {
            baseUrl = alg::trim_copy(base_sel.nodeAt(0).attribute("href"));
        }
        else
        {
            baseUrl = getBasePart(url);
        }	
        
        CSelection sel = doc.find("a[href], link[href][rel=\"stylesheet\"], script[src]");
        for(unsigned i = 0; i < sel.nodeNum(); ++i)
        {
            CNode node = sel.nodeAt(i);
            std::string us = node.tag() == "script" ? node.attribute("src")
            : node.attribute("href");
            
            alg::trim(us);
            if(us.empty() || us.front() == '#') continue;
            
            uri::uri u;
            if(alg::starts_with(us, "//"))
            {
                uri::builder b(u);
                b.set_scheme(baseUrl.scheme());
                u.append(us.substr(2));
            }
            else
            {
                u = us;
            }
            if(is_absolute(u))
            {
                if (!(u.host() == baseUrl.host() && u.port() == baseUrl.host())) continue;
            }
            else
            {
                uri::uri tmp(baseUrl);
                if(us.front() != '/') tmp.append("/");
                tmp.append(us);
                u = tmp;
            }
            
            uri::uri p = removeFragmentPart(u);
            res.insert(p);
        }
    }
    
    return res;
}

int main(int argc, char * argv[])
{
    if (argc != 3)
    {
        std::cerr << "expected args: <URL> <path>" << std::endl;
        return -1;
    }
    using namespace boost::network;
    using namespace boost::network::http;
    
    
    uri::uri baseUrl (removeFragmentPart(uri::uri(argv[1])));
    fs::path basePath (argv[2]);
    std::set<uri::uri> downloaded;
    std::set<uri::uri> links;
    std::list<std::future<std::set<boost::network::uri::uri>>> futures;
    links.insert(baseUrl);
    
    while(links.size() > 0 || futures.size() > 0)
    {
        if(links.size() > 0 && futures.size() < 33)
        {
            uri::uri u = *links.begin();
            links.erase(links.begin());
            if (downloaded.count(u) == 1)
            {
                continue;
            }
            auto f = std::async(std::launch::async, downloadAndParse, u, basePath);
            futures.push_back(std::move(f));
            downloaded.insert(u);
        }
        
        for(auto i = futures.begin(); i != futures.end(); )
        {
            if (i->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto res = i->get();
                links.insert(res.begin(),res.end());
                
                std::set_difference(res.begin(), res.end(), downloaded.begin(), downloaded.end(), std::inserter(links, links.end()));
                i = futures.erase(i);
            }
            else
            {
                ++i;
            }
        }
    }
    
    std::cout << "Downloaded: " << downloaded.size() << std::endl;
    return 0;
}
