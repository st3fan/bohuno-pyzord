// license.cpp

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#include "license.hpp"
#include "arcfour.hpp"
#include "base64_decoder.hpp"

namespace bohuno {

   license::license(boost::filesystem::path const& path)
   {
      if (!boost::filesystem::exists(path)) {
         throw std::runtime_error(std::string("Cannot load license; file does not exist: ") + path.string());
      }

      std::string license;

      std::ifstream file;
      file.open(path.string().c_str());      
      if (!file) {
         throw std::runtime_error("Cannot open license file for reading");
      }
      file >> license;
      file.close();
      
      this->parse(license);
   }

   license::license(std::string const& license)
   {
      this->parse(license);
   }
   
   std::string license::decode(std::string const& license)
   {
      std::string decoded;

      boost::iostreams::filtering_istream in;
      in.push(bohuno::iostreams::arcfour_filter("Work it, make it, do it, makes us harder, better, faster, STRONGER!"));
      in.push(bohuno::iostreams::base64_decoder());
      in.push(boost::make_iterator_range(license));

      boost::iostreams::filtering_ostream out;
      out.push(boost::iostreams::back_inserter(decoded));

      boost::iostreams::copy(in, out);
      out.reset();

      return decoded;
   }

   void license::parse(std::string const& license)
   {
      std::string decoded = decode(license);
      std::vector<std::string> components;
      boost::algorithm::split(components, decoded, boost::algorithm::is_any_of("\t"));

      if (components.size() != 3) {
         throw std::runtime_error("Invalid license key; failed to parse");
      }

      realname_ = components[0];
      username_ = components[1];
      password_ = components[2];
   }

   std::string const& license::realname()
   {
      return realname_;
   }
   
   std::string const& license::username()
   {
      return username_;
   }
   
   std::string const& license::password()
   {
      return password_;
   }

}
