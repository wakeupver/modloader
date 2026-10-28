/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "gtadat.hpp"
#include "../util/strutil.hpp"

namespace mla {

static std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t b = 0;
    while(b < text.size())
    {
        size_t e = text.find('\n', b);
        if(e == std::string::npos) e = text.size();
        size_t len = e - b;
        if(len > 0 && text[b + len - 1] == '\r') --len;
        lines.push_back(text.substr(b, len));
        b = e + 1;
    }
    return lines;
}

/* first token of a line in upper case ("" for blank lines) */
static std::string FirstToken(const std::string& line)
{
    size_t b = 0;
    while(b < line.size() && std::isspace((unsigned char)line[b])) ++b;
    size_t e = b;
    while(e < line.size() && !std::isspace((unsigned char)line[e])) ++e;
    std::string tok = line.substr(b, e - b);
    for(auto& c : tok) c = (char)std::toupper((unsigned char)c);
    return tok;
}

static std::string RestOfLine(const std::string& line)
{
    size_t b = 0;
    while(b < line.size() && std::isspace((unsigned char)line[b])) ++b;
    while(b < line.size() && !std::isspace((unsigned char)line[b])) ++b;
    return Trim(line.substr(b));
}

std::set<std::string> GtaDatReferencedNames(const std::string& dat)
{
    std::set<std::string> names;
    for(auto& line : SplitLines(dat))
    {
        std::string tok = FirstToken(line);
        if(tok != "IDE" && tok != "IPL") continue;
        std::string path = RestOfLine(line);
        if(path.empty()) continue;
        names.insert(BaseName(NormalizeKey(path)));
    }
    return names;
}

std::string MergeGtaDat(const std::string& original, const std::vector<GtaDatItem>& items)
{
    if(items.empty())
        return original;

    const char* eol = (original.find("\r\n") != std::string::npos)? "\r\n" : "\n";
    std::vector<std::string> lines = SplitLines(original);

    size_t firstIpl = lines.size();
    for(size_t i = 0; i < lines.size(); ++i)
        if(FirstToken(lines[i]) == "IPL") { firstIpl = i; break; }

    // Where "the end" is: before a final EXIT line, if the file has one
    size_t endPos = lines.size();
    for(size_t i = lines.size(); i-- > 0; )
    {
        std::string t = Trim(lines[i]);
        if(t.empty()) continue;
        if(FirstToken(t) == "EXIT" && t.size() == 4) endPos = i;
        break;
    }

    size_t idePos = (firstIpl < endPos)? firstIpl : endPos;

    std::string out;
    out.reserve(original.size() + items.size() * 48);

    auto putLine = [&](const std::string& s) { out += s; out += eol; };

    for(size_t i = 0; i < lines.size(); ++i)
    {
        if(i == idePos)
            for(auto& it : items) if(it.kind == "IDE") putLine("IDE " + it.path);
        if(i == endPos)
            for(auto& it : items) if(it.kind == "IPL") putLine("IPL " + it.path);
        putLine(lines[i]);
    }
    if(idePos >= lines.size())
        for(auto& it : items) if(it.kind == "IDE") putLine("IDE " + it.path);
    if(endPos >= lines.size())
        for(auto& it : items) if(it.kind == "IPL") putLine("IPL " + it.path);

    return out;
}

} // namespace mla
