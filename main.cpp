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

void isDownloadNeeded(const boost::network::uri::uri& uri, const fs::path& p, std::string& body_)
{
    using namespace boost::network;
    using namespace boost::network::http;
    
    if(!fs::exists(p))
    {
        client::request request_(uri);
        client client_;
        client::response response_ = client_.head(request_);
        
        auto rCType = headers(response_)["Content-Type"];
        if (rCType)
        {
            const auto ctype = rCType.front().second;
            if (  alg::starts_with(ctype , "text/html") 
                || alg::starts_with(ctype , "text/css")
                || alg::starts_with(ctype , "application/javascript")
            )
            {
                client::response response_ = client_.get(request_);
                body_ = body(response_);
                saveFile(p, body_);
            }
        }
    }
    else
    {
        loadFile(p, body_);
    }
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

std::set<boost::network::uri::uri> downloadAndParse(const boost::network::uri::uri& url, const fs::path& basePath)
{
    using namespace boost::network;
    using namespace boost::network::http;
    
    std::set<boost::network::uri::uri> res;
    std::string query = url.query();
    fs::path rel_path = url.path();
    fs::path file2Save = basePath / (url.path() + (query.empty() ? std::string() : '?' + query)) / "index.html";
    
    std::cout << url.string() << " -> " << file2Save << std::endl;
    
    std::string body_;
    isDownloadNeeded(url, file2Save, body_);
    {
        CDocument doc;
        doc.parse(body_.c_str());
        
        uri::uri baseUrl;
        
        CSelection base_sel = doc.find("head base[href]");
        if(base_sel.nodeNum() > 0)
        {
            baseUrl = base_sel.nodeAt(0).attribute("href");
        }
        else
        {
            uri::builder b(baseUrl);
            b.set_scheme(url.scheme());
            b.set_host(url.host());
            if(!url.port().empty()) b.set_port(url.port());
        }	
        
        CSelection sel = doc.find("a[href], link[href][rel=\"stylesheet\"], script[src]");
        for(int i = 0; i < sel.nodeNum(); ++i)
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
        std::cerr << "Enter Url" << std::endl;
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
        if(links.size() > 0 && futures.size() < 21)
        {
            uri::uri u = *links.begin();
            links.erase(links.begin());
            if (downloaded.count(u) == 1)
            {
                continue;
            }
            auto f = std::async(std::launch::async, downloadAndParse, u, basePath);
            /*  f.wait();
             *        auto res = f.get();
             *	links.insert(res.begin(),res.end());
             *  
             *	std::set_difference(res.begin(), res.end(), downloaded.begin(), downloaded.end(), std::inserter(links, links.end()));
             */futures.push_back(std::move(f));
                   downloaded.insert(u); // ???
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
        //std::cout << "Links size: " << links.size() << " | Downloaded size: " << downloaded.size() << std::endl;
    }
    
    std::cout << "Links: " << links.size() << " | Downloaded: " << downloaded.size() << std::endl;
    return 0;
}
