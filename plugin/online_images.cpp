#include "online_images.hpp"
#include "image.hpp"
#include "flcat/paths.hpp"
#include "flcat/scanner.hpp"
#include <winhttp.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace artwork {
namespace fs=std::filesystem;
namespace {
struct InternetHandle {
    void operator()(void* handle) const { WinHttpCloseHandle(handle); }
};
using Handle=std::unique_ptr<void,InternetHandle>;

std::string request(const wchar_t* host,const std::string& path,size_t limit,const std::atomic<bool>& stop);
std::string request(const wchar_t* host,const std::string& path,size_t limit,const std::atomic<bool>& stop,int redirects) {
    Handle session(WinHttpOpen(L"FL Plugin Atlas/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
    if(!session) throw std::runtime_error("网络不可用");
    WinHttpSetTimeouts(session.get(),3000,3000,4000,4000);
    Handle connection(WinHttpConnect(session.get(),host,INTERNET_DEFAULT_HTTPS_PORT,0));
    auto target=fs::u8path(path).wstring();
    Handle req(WinHttpOpenRequest(connection.get(),L"GET",target.c_str(),nullptr,nullptr,nullptr,WINHTTP_FLAG_SECURE));
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!req || !WinHttpSetOption(req.get(),WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof policy) ||
       !WinHttpSendRequest(req.get(),nullptr,0,nullptr,0,0,0) || !WinHttpReceiveResponse(req.get(),nullptr))
        throw std::runtime_error("联网获取失败");
    DWORD code=0,size=sizeof code;
    WinHttpQueryHeaders(req.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr);
    if((code==301 || code==302 || code==303 || code==307 || code==308) && redirects<2) {
        wchar_t location[2048]{};
        DWORD bytes=sizeof location;
        if(!WinHttpQueryHeaders(req.get(),WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location,&bytes,WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error("图片服务暂时不可用");
        std::wstring next=location;
        auto scheme=next.find(L"://");
        if(scheme!=next.npos) {
            auto slash=next.find(L'/',scheme+3);
            next=slash==next.npos?L"/" : next.substr(slash);
        }
        if(next.empty() || next[0]!=L'/') throw std::runtime_error("图片服务暂时不可用");
        std::string narrowed;
        for(wchar_t c:next) {
            if(c<32 || c>126) throw std::runtime_error("图片服务暂时不可用");
            narrowed+=static_cast<char>(c);
        }
        return request(host,narrowed,limit,stop,redirects+1);
    }
    if(code==404) return {};
    if(code!=200) throw std::runtime_error("图片服务暂时不可用");
    std::string bytes;
    char buffer[16384];
    while(!stop) {
        DWORD n=0;
        if(!WinHttpReadData(req.get(),buffer,sizeof buffer,&n) || bytes.size()+n>limit)
            throw std::runtime_error("响应无效或过大");
        if(!n) return bytes;
        bytes.append(buffer,n);
    }
    return {};
}
std::string request(const wchar_t* host,const std::string& path,size_t limit,const std::atomic<bool>& stop) {
    return request(host,path,limit,stop,0);
}

std::string slug(const std::string& s) {
    std::string out;
    bool separator=false;
    for(unsigned char c:s) {
        if(c>=128) return {}; // unsupported script: never match a partial name
        if(!std::isalnum(c)) {separator=true; continue;}
        if(separator && !out.empty()) out+='-';
        out+=static_cast<char>(std::tolower(c)); separator=false;
    }
    return out;
}
struct Product {std::string brand, name;};
Product identity(const flcat::Entry& e) {
    if(e.identity_conflict || !e.available) return {};
    auto brand=slug(e.vendor), name=slug(e.name);
    if(brand.empty() || name.empty()) return {};
    if(brand=="valhalla-dsp-llc") brand="valhalla-dsp";
    if(name.rfind(brand+"-",0)==0) name.erase(0,brand.size()+1);
    for(const std::string suffix:{"-mono-stereo","-stereo","-mono"}) {
        if(name.size()>suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0) {
            name.resize(name.size()-suffix.size()); break;
        }
    }
    if(brand.size()>100 || name.size()>120) return {};
    return {brand,name};
}
std::string compact(std::string s) {
    s.erase(std::remove(s.begin(),s.end(),'-'),s.end()); return s;
}
std::string between(const std::string& s,const std::string& start,const std::string& end,size_t pos=0) {
    auto a=s.find(start,pos); if(a==s.npos) return {};
    a+=start.size(); auto b=s.find(end,a);
    return b==s.npos?std::string{}:s.substr(a,b-a);
}
fs::path cache_root() {return flcat::data_home()/"FL Plugin Atlas"/"Online images";}
fs::path cache_file(const Product& p) {return cache_root()/("brand-"+p.brand)/("plugin-"+p.name+".img");}
// Cache paths stay on the catalog slug. The site lookup can use a shorter brand name.
std::string watch_brand(std::string brand) {
    if(brand=="roland-cloud") return "roland";
    if(brand=="xfer-records") return "xfer";
    return brand;
}
bool described(const std::string& wanted,const std::string& listed) {
    if(compact(wanted)==compact(listed)) return true;
    if(wanted.size()<6) return false;
    const std::string prefix=wanted+"-";
    if(listed.rfind(prefix,0)!=0) return false;
    const auto rest=listed.substr(prefix.size());
    for(const char* item:{"compressor","limiter","compressor-limiter","equalizer","eq","reverb","delay",
                          "saturator","exciter","de-esser","channel-strip","plugin"})
        if(rest==item) return true;
    return false;
}
std::string encode_query(const std::string& text) {
    static const char hex[]="0123456789ABCDEF";
    std::string out;
    for(unsigned char c:text) {
        if(std::isalnum(c) || c=='-' || c=='_' || c=='.') out+=char(c);
        else if(c==' ') out+='+';
        else {out+='%'; out+=hex[c>>4]; out+=hex[c&15];}
    }
    return out;
}
struct Page {std::string brand,name,listed,image; bool plugin_alliance=false;};
Page parse_page(const std::string& html) {
    Page page;
    size_t begin=0;
    while((begin=html.find("<script type=\"application/ld+json\">",begin))!=html.npos) {
        auto json=between(html,"<script type=\"application/ld+json\">","</script>",begin++);
        if(json.find("\"@type\":\"Product\"")==json.npos) continue;
        auto brand=slug(between(json,"\"brand\":{\"@type\":\"Brand\",\"name\":\"","\""));
        auto raw_name=between(json,"\"name\":\"","\"");
        auto listed=slug(raw_name);
        flcat::Entry candidate; candidate.vendor=brand; candidate.name=raw_name;
        auto id=identity(candidate);
        if(id.brand.empty() || id.name.empty() || listed.empty()) continue;
        auto key=between(json,"\"image\":\"https://images.plugin.watch/images/","/");
        if(key.empty() || key.size()>64 ||
           key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=key.npos)
            continue;
        page.brand=id.brand; page.name=id.name; page.listed=listed; page.image="/images/"+key+"/";
        const std::string seller="\"seller\":{\"@type\":\"Organization\",\"name\":\"";
        for(size_t pos=0;(pos=json.find(seller,pos))!=json.npos;) {
            auto name=between(json,seller,"\"",pos);
            pos+=seller.size();
            if(slug(name)=="plugin-alliance") page.plugin_alliance=true;
        }
        return page;
    }
    return {};
}
bool distributor_match(const Product& wanted,const Page& page) {
    if(wanted.brand!="plugin-alliance" || !page.plugin_alliance || page.brand.empty()) return false;
    if(compact(page.listed)==compact(wanted.name)) return true;
    const auto rebuilt=page.listed.rfind(page.brand+"-",0)==0 ? page.listed : page.brand+"-"+page.name;
    return compact(rebuilt)==compact(wanted.name);
}
bool accept_page(const Product& wanted,const Page& page) {
    if(page.image.empty()) return false;
    if(page.brand==watch_brand(wanted.brand) && described(wanted.name,page.name)) return true;
    return distributor_match(wanted,page);
}
struct Hit {std::string brand,slug;};
std::vector<Hit> search_hits(const std::string& query,const std::atomic<bool>& stop) {
    std::vector<Hit> hits;
    if(query.size()<3) return hits;
    auto html=request(L"plugin.watch","/search?q="+encode_query(query.substr(0,80)),1024*1024,stop);
    const std::string prefix="href=\"/products/";
    for(size_t pos=0;(pos=html.find(prefix,pos))!=html.npos && hits.size()<40;) {
        auto link=between(html,prefix,"\"",pos);
        pos+=prefix.size();
        if(link.empty() || link.find('?')!=link.npos || link.find("..")!=link.npos) continue;
        auto slash=link.find('/');
        if(slash==link.npos || slash==0 || slash+1>=link.size()) continue;
        Hit hit{link.substr(0,slash),link.substr(slash+1)};
        if(slug(hit.brand)!=hit.brand || slug(hit.slug)!=hit.slug) continue;
        hits.push_back(std::move(hit));
    }
    return hits;
}
fs::path miss_path(const Product& p) {return cache_root()/"misses"/("brand-"+p.brand+"-plugin-"+p.name+".txt");}
bool fresh_miss(const Product& p) {
    std::ifstream in(miss_path(p));
    long long stamp=0;
    if(!(in>>stamp)) return false;
    const auto now=static_cast<long long>(std::time(nullptr));
    return stamp<=now && now-stamp<7LL*24*60*60;
}
void note_miss(const Product& p) {
    std::error_code ec; fs::create_directories(miss_path(p).parent_path(),ec);
    std::ofstream out(miss_path(p),std::ios::trunc);
    if(out) out<<std::time(nullptr);
}

// Search links belonging to the exact brand, retaining distinct version numbers.
std::map<std::string,std::string> products(const std::string& brand,const std::atomic<bool>& stop) {
    auto site=watch_brand(brand);
    auto html=request(L"plugin.watch","/brands/"+site,4*1024*1024,stop);
    std::string prefix="href=\"/products/"+site+"/";
    std::map<std::string,std::string> links;
    for(size_t pos=0; (pos=html.find(prefix,pos))!=html.npos;) {
        auto name=between(html,prefix,"\"",pos);
        pos+=prefix.size();
        if(name.empty() || slug(name)!=name) continue;
        auto [it,inserted]=links.emplace(compact(name),name);
        if(!inserted && it->second!=name) it->second.clear();
    }
    return links;
}
bool download(const std::string& path,const fs::path& dest,const std::atomic<bool>& stop);
std::string link_for(const std::map<std::string,std::string>& links,const std::string& name) {
    auto exact=links.find(compact(name));
    if(exact!=links.end() && !exact->second.empty() && described(name,exact->second)) return exact->second;
    std::string found;
    for(const auto& item:links) {
        if(item.second.empty() || !described(name,item.second)) continue;
        if(!found.empty() && found!=item.second) return {};
        found=item.second;
    }
    return found;
}
bool save_page(const Product& wanted,const std::string& site_brand,const std::string& slug,
               const fs::path& dest,const std::atomic<bool>& stop) {
    if(site_brand.empty() || slug.empty() || slug.find('/')!=slug.npos || slug.find('?')!=slug.npos) return false;
    auto html=request(L"plugin.watch","/products/"+site_brand+"/"+slug,3*1024*1024,stop);
    if(html.empty() || stop) return false;
    auto page=parse_page(html);
    return accept_page(wanted,page) && download(page.image,dest,stop);
}
bool lookup(const Product& wanted,const std::string& query,const std::map<std::string,std::string>& links,
            const fs::path& dest,const std::atomic<bool>& stop) {
    auto site=watch_brand(wanted.brand);
    auto listed=link_for(links,wanted.name);
    if(!listed.empty() && save_page(wanted,site,listed,dest,stop)) return true;
    if(stop) return false;
    if(save_page(wanted,site,wanted.name,dest,stop)) return true;
    if(stop || query.size()<3) return false;
    int fetched=0;
    for(const auto& hit:search_hits(query,stop)) {
        if(stop) return false;
        bool same=hit.brand==site && described(wanted.name,hit.slug);
        bool distributed=wanted.brand=="plugin-alliance" &&
            (compact(hit.slug)==compact(wanted.name) || compact(hit.brand+"-"+hit.slug)==compact(wanted.name));
        if(!same && !distributed) continue;
        if(++fetched>2) break;
        if(save_page(wanted,hit.brand,hit.slug,dest,stop)) return true;
    }
    return false;
}
bool download(const std::string& path,const fs::path& dest,const std::atomic<bool>& stop) {
    fs::create_directories(dest.parent_path());
    auto temp=dest;
    temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
    struct Cleanup {fs::path path; ~Cleanup(){std::error_code ec;fs::remove(path,ec);}} cleanup{temp};
    // JPEG is available even on Windows installs without an AVIF codec.
    for(auto size:{"md16x9.avif","share.jpg"}) {
        auto bytes=request(L"images.plugin.watch",path+size,4*1024*1024,stop);
        if(stop) return false;
        if(bytes.empty()) continue;
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);
        output.write(bytes.data(),bytes.size()); output.close();
        if(!output) throw std::runtime_error("无法写入图片缓存");
        PreviewImage check;
        if(!check.load(temp)) continue;
        if(!MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("保存产品图失败");
        return true;
    }
    return false;
}
}

fs::path cached(const flcat::Entry& entry) {
    auto p=identity(entry);
    return p.brand.empty()?fs::path{}:cache_file(p);
}
Report fill(const fs::path& library,const std::vector<flcat::Entry>& entries,const std::atomic<bool>& stop) {
    Report report;
    std::set<std::string> local_images;
    for(const auto& e:entries) {
        if(stop) return report;
        if(!e.available || local_images.count(e.plugin_key)) continue;
        PreviewImage check;
        if(!e.custom_image.empty() && check.load(fs::u8path(e.custom_image))) local_images.insert(e.plugin_key);
        else for(auto& file:flcat::thumbnail_files(library,e.path))
            if(check.load(file)){local_images.insert(e.plugin_key);break;}
    }
    std::map<std::string,std::map<std::string,std::string>> wanted;
    for(const auto& e:entries) {
        auto p=identity(e);
        if(p.brand.empty() || local_images.count(e.plugin_key)) continue;
        auto& query=wanted[p.brand][p.name];
        if(query.empty()) query=e.name;
    }
    for(auto& [brand,names]:wanted) {
        std::map<std::string,std::string> links;
        try {links=products(brand,stop);}
        catch(const std::exception&) {report.errors++;}
        for(const auto& [name,query]:names) {
            if(stop) return report;
            Product p{brand,name};
            try {
                auto dest=cache_file(p);
                PreviewImage check;
                if(fs::is_regular_file(dest) && check.load(dest)) continue;
                if(fresh_miss(p)) {report.missing++; continue;}
                if(lookup(p,query,links,dest,stop)) {
                    report.saved++;
                    std::error_code ec; fs::remove(miss_path(p),ec);
                } else if(!stop) {
                    report.missing++; note_miss(p);
                }
            } catch(const std::exception&) {
                report.errors++;
            }
        }
        std::clog<<"artwork "<<brand<<" total saved "<<report.saved<<" missing "<<report.missing<<"\n"<<std::flush;
    }
    return report;
}
}
