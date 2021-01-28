#include "Options.h"

//#include <unistd.h>
#include <iostream>
#include <fstream>

#ifdef __APPLE__
extern char** environ;
#endif

const Options gNullOptions;

//unsigned int Options::load(const std::string& prefix, bool replace)
//{
//    char** envPtr = environ;
//    int numOptions = 0;
//    while (*envPtr)
//    {
//        std::string env(*envPtr);
//        std::string::size_type pos = env.find_first_of("=", 1);
//        if (pos != std::string::npos)
//        {
//            if (env.substr(0, prefix.length()) == prefix)
//            {
//                std::string name = env.substr(prefix.length(), pos - prefix.length());
//                if (replace)
//                    mOptionMap.erase(name);
//                std::string value = env.substr(pos + 1);
//                if (value == "true" || value == "false")
//                {
//                    add(name, value == "true");
//                }
//                else if (value.find_first_not_of("0123456789.") == std::string::npos)
//                {
//                    if (value.find_first_of(".") == std::string::npos)
//                    {
//                        add(name, stoi(value));
//                    }
//                    else
//                    {
//                        add(name, stod(value));
//                    }
//                }
//                else
//                {
//                    add(name, value);
//                }
//                numOptions++;
//            }
//        }
//        envPtr++;
//    }
//    return(numOptions);
//}

//void Options::dump( unsigned int level ) const
//{
//    for ( OptionMap::const_iterator iter = mOptionMap.begin(); iter != mOptionMap.end(); iter++ )
//    {
//        //printf( "%s == %s\n", iter->second->type().c_str(), typeid(*this).name() );
//        if ( iter->second->type() == typeid(*this).name() )
//        {
//            std::cout << std::string(level+1,'.') << iter->first << " (" << iter->second->type() << ") => " << std::endl;
//            //printf( "%*s%s (%s) =>\n", level*2, "", iter->first.c_str(), iter->second->type().c_str() );
//            _Option *optionPtr = iter->second.get();
//            Option<Options> *optionsPtr = dynamic_cast<Option<Options> *>(optionPtr);
//            optionsPtr->value().dump( level+1 );
//        }
//        else
//        {
//            std::cout << std::string(level+1,'.') << iter->first << " (" << iter->second->type() << ") => " <<  iter->second->valueString() << std::endl;
//            //printf( "%*s%s (%s) => %s\n", level*2, "", iter->first.c_str(), iter->second->type().c_str(), iter->second->valueString().c_str() );
//        }
//    }
//}

void to_json(json& j, const Options& o)
{
    o.jsonDump(j);
}

void Options::jsonDump(json& j) const
{
    for (OptionMap::const_iterator iter = mOptionMap.begin(); iter != mOptionMap.end(); iter++)
    {
        iter->second->jsonDump(j, iter->first);
    }
}

void from_json(const json& j, Options& o)
{
    // Not used
}

void Options::jsonLoad(const json& j)
{
    for (json::const_iterator iter = j.begin(); iter != j.end(); iter++)
    {
#if OPTIONS_DEBUG
        printf("%s = ", iter.key().c_str());
#endif 

        if (iter->is_null())
        {
#if OPTIONS_DEBUG
            printf("null");
#endif // OPTIONS_DEBUG

        }
        if (iter->is_boolean())
        {
#if OPTIONS_DEBUG
            printf("bool");
#endif // OPTIONS_DEBUG

            add(iter.key(), iter->get<bool>());
        }
        else if (iter->is_number())
        {
#if OPTIONS_DEBUG
            printf("number_");
#endif 
            if (iter->is_number_integer())
            {
#if OPTIONS_DEBUG
                printf("integer");
#endif 
                add(iter.key(), iter->get<int>());
            }
            else if (iter->is_number_unsigned())
            {
#if OPTIONS_DEBUG
                printf("unsigned");
#endif 
                add(iter.key(), iter->get<unsigned int>());
            }
            else if (iter->is_number_float())
            {
#if OPTIONS_DEBUG
                printf("float");
#endif 
                add(iter.key(), iter->get<double>());
            }
            else
            {
#if OPTIONS_DEBUG
                printf("unknown");
#endif 
            }
        }
        else if (iter->is_string())
        {
#if OPTIONS_DEBUG
            printf("string/%s", iter->get<std::string>().c_str());
#endif 
            add(iter.key(), iter->get<std::string>());
        }
        else if (iter->is_object())
        {
#if OPTIONS_DEBUG
            printf("object");
#endif 
            Options options;
            options.jsonLoad(*iter);
            add(iter.key(), options);

        }
        else if (iter->is_array())
        {
#if OPTIONS_DEBUG
            printf("array");
#endif 
        }
        else
        {
#if OPTIONS_DEBUG
            printf("unknown");
#endif 
        }
#if OPTIONS_DEBUG
        printf("\n");
#endif 
        //iter->second->jsonDump( j, iter->first );
    }
}

bool Options::readFile(const std::string& filename)
{
    try {
        std::ifstream file;
        file.exceptions(std::ifstream::badbit);
        file.open(filename);
        json j;
        file >> j;
        // TODO: remove after debug
        //std::cout << j.dump(4) << std::endl;
        jsonLoad(j);
    }
    catch (const std::exception & e)
    {
        //Error( "Unable to read options from file '%s': %s (%s)", filename.c_str(), e.what(), std::strerror(errno) );
        return(false);
    }
    return(true);
}

bool Options::writeFile(const std::string& filename, bool pretty)
{
    try {
        std::ofstream file;
        file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        file.open(filename);
        json j;
        jsonDump(j);
        if (pretty)
            file << std::setw(2) << j << std::endl;
        else
            file << j << std::endl;
    }
    catch (const std::exception & e)
    {
        //Error( "Unable to write options to file '%s': %s (%s)", filename.c_str(), e.what(), std::strerror(errno) );
        return(false);
    }
    return(true);
}
