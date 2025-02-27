/**
   @author Shin'ichiro Nakaoka
*/

#include "OptionManager.h"
#include "MessageView.h"
#include <fmt/format.h>
#include <iostream>
#include <set>
#include "gettext.h"

using namespace std;
using namespace cnoid;
namespace program_options = boost::program_options;

namespace {

struct OptionInfo {
    OptionInfo() : options("Options") { }
    program_options::options_description options;
    program_options::positional_options_description positionalOptions;
    program_options::variables_map variables;
};

OptionInfo* info = nullptr;

vector<string> inputFiles;
Signal<void(std::vector<std::string>& inputFiles)> sigInputFileOptionsParsed_[2];

Signal<void(boost::program_options::variables_map& variables)> sigOptionsParsed_[2];

}


bool OptionManager::parseCommandLine1(int argc, char *argv[])
{
    info->options.add_options()("input-file", program_options::value<vector<string>>(), "general input file");
    info->positionalOptions.add("input-file", -1);

    info->options.add_options()("help,h", "show help message");

    bool doContinue = true;
    
    try{
        program_options::store(
            program_options::command_line_parser(argc, argv).
            options(info->options).positional(info->positionalOptions).run(), info->variables);
        program_options::notify(info->variables);

        if(info->variables.count("input-file")){
            inputFiles = info->variables["input-file"].as<vector<string>>();
        }
        
    } catch (std::exception& ex) {
        std::cerr << "Command line option error! : " << ex.what() << std::endl;
        doContinue = false;
    }

    if(!doContinue || info->variables.count("help")){
        cout << info->options << endl;
        doContinue = false;
        
    } else {
        sigOptionsParsed_[0](info->variables);
        sigInputFileOptionsParsed_[0](inputFiles);
    }

    return doContinue;
}


void OptionManager::parseCommandLine2()
{
    sigInputFileOptionsParsed_[1](inputFiles);
    sigOptionsParsed_[1](info->variables);
    
    // The destructors of the OptionInfo members should be executed here
    // because their elements may be driven by the code instantiated in the plug-in dlls.
    // If the destructors are called when the program is finished after the plug-ins are
    // unloaded, the destructors may cause the segmentation fault
    // by calling the non-existent codes.
    delete info;
    info = nullptr;
    sigOptionsParsed_[0].disconnectAllSlots();
    sigOptionsParsed_[1].disconnectAllSlots();

    for(auto& file : inputFiles){
        MessageView::instance()->putln(
            fmt::format(_("Input file \"{}\" was not processed."), file),
            MessageView::Warning);
    }
}


OptionManager::OptionManager()
{
    info = new OptionInfo;
}


OptionManager::~OptionManager()
{

}


OptionManager& OptionManager::addOption(const char* name, const char* description)
{
    if(info){
        info->options.add_options()(name, description);
    }
    return *this;
}


OptionManager& OptionManager::addOption(const char* name, const program_options::value_semantic* s)
{
    if(info){
        info->options.add_options()(name, s);
    }
    return *this;
}


OptionManager& OptionManager::addOption(const char* name, const program_options::value_semantic* s, const char* description)
{
    if(info){
        info->options.add_options()(name, s, description);
    }
    return *this;
}


/*
OptionManager& OptionManager::addPositionalOption(const char* name, int maxCount)
{
    if(info){
        info->positionalOptions.add(name, maxCount);
    }
    return *this;
}
*/


SignalProxy<void(std::vector<std::string>& inputFiles)> OptionManager::sigInputFileOptionsParsed(int phase)
{
    if(phase < 0){
        phase = 0;
    } else if(phase > 1){
        phase = 1;
    }
    return sigInputFileOptionsParsed_[phase];
}


SignalProxy<void(boost::program_options::variables_map& variables)> OptionManager::sigOptionsParsed(int phase)
{
    if(phase < 0){
        phase = 0;
    } else if(phase > 1){
        phase = 1;
    }
    return sigOptionsParsed_[phase];
}
