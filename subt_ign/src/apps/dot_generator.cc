/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <ignition/common/Console.hh>
#include "ConnectionHelper.hh"
#include "SdfParser.hh"

using namespace subt;
using namespace ignition;

/// \brief Print usage
void usage()
{
  std::cerr << "Usage: dot_generator [--finals] <path_to_world_sdf_file>"
            << std::endl;
}

/// \brief Print the DOT file
/// \param[in] _vertexData vector of vertex data containing
/// vertex and edge info
/// \param[in] _circuit Empty string or "--finals" .
void printGraph(std::vector<VertexData> &_vertexData,
                const std::string &_circuit)
{
  std::stringstream out;
  out << "/* Visibility graph generated by dot_generator */\n\n";
  out << "graph {\n";
  out << "  /* ==== Vertices ==== */\n\n";
//  out << "0  [label=\"0::base_station::BaseStation\"]\n";

  for (const auto &vd : _vertexData)
  {
    // update staging area name for compatibility with other subt tools that
    // rely on this naming convention
    std::string name = vd.tileName;
    std::string type = vd.tileType;

    if (type.find("Blocker") != std::string::npos)
    {
      continue;
    }

    if (type == "Cave Starting Area Type B" ||
        type == "Urban Starting Area" ||
        type == "Finals Staging Area")
    {
      type = "base_station";
      name = "BaseStation";
      out << "  /* Base station / Staging area */\n";
    }
    out << "  " << vd.id;
    if (vd.id < 10)
      out << " ";
    out << "  " << "[label=\"" << vd.id << "::" << type
        << "::" << name << "\"];\n";
    if (type == "base_station")
      out << std::endl;
  }

  out << "\n  /* ==== Edges ==== */\n\n";

  if (!_vertexData.empty())
  {
    for (unsigned int i = 0u; i < _vertexData.size() -1; ++i)
    {
      for (unsigned int j = i+1; j < _vertexData.size(); ++j)
      {
        math::Vector3d point;
        if (subt::ConnectionHelper::ComputePoint(
              &_vertexData[i], &_vertexData[j], point))
        {
          int cost = 1;
          auto tp1 =
            subt::ConnectionHelper::connectionTypes[_vertexData[i].tileType];
          auto tp2 =
            subt::ConnectionHelper::connectionTypes[_vertexData[j].tileType];

          // Get the circuit type for each tile (cave, urban, etc.)
          auto ct1It = subt::ConnectionHelper::circuitTypes.find(
            _vertexData[i].tileType);
          if (ct1It == subt::ConnectionHelper::circuitTypes.end())
          {
            ignwarn << "No circuit information for: " << _vertexData[i].tileType
                    << std::endl;
          }
          auto ct2It = subt::ConnectionHelper::circuitTypes.find(
            _vertexData[j].tileType);
          if (ct2It == subt::ConnectionHelper::circuitTypes.end())
          {
            ignwarn << "No circuit information for: " << _vertexData[j].tileType
                    << std::endl;
          }

          // Is one of the tile a starting area? If so, the cost should be 1.
          bool connectsToStaging =
            _vertexData[i].tileType == "Cave Starting Area Type B" ||
            _vertexData[i].tileType == "Urban Starting Area" ||
            _vertexData[j].tileType == "Cave Starting Area Type B" ||
            _vertexData[j].tileType == "Urban Starting Area" ||
            _vertexData[j].tileType == "Finals Staging Area";

          if ((tp1 == subt::ConnectionHelper::STRAIGHT &&
                tp2 == subt::ConnectionHelper::STRAIGHT) || connectsToStaging)
            cost = 1;
          else if (tp1 == subt::ConnectionHelper::TURN &&
              tp2 == subt::ConnectionHelper::STRAIGHT)
          {
            // Both tiles are tunnels
            if (_circuit == "--finals"                              &&
                ct1It != subt::ConnectionHelper::circuitTypes.end() &&
                ct1It->second == subt::ConnectionHelper::TUNNEL     &&
                ct2It != subt::ConnectionHelper::circuitTypes.end() &&
                ct2It->second == subt::ConnectionHelper::TUNNEL)
              cost = 2;
            else
              cost = 3;
          }
          else if (tp1 == subt::ConnectionHelper::STRAIGHT &&
              tp2 == subt::ConnectionHelper::TURN)
          {
            // Both tiles are tunnels
            if (_circuit == "--finals"                              &&
                ct1It != subt::ConnectionHelper::circuitTypes.end() &&
                ct1It->second == subt::ConnectionHelper::TUNNEL     &&
                ct2It != subt::ConnectionHelper::circuitTypes.end() &&
                ct2It->second == subt::ConnectionHelper::TUNNEL)
              cost = 2;
            else
              cost = 3;
          }
          else
          {
            // Both tiles are tunnels
            if (_circuit == "--finals"                              &&
                ct1It != subt::ConnectionHelper::circuitTypes.end() &&
                ct1It->second == subt::ConnectionHelper::TUNNEL     &&
                ct2It != subt::ConnectionHelper::circuitTypes.end() &&
                ct2It->second == subt::ConnectionHelper::TUNNEL)
              cost = 3;
            else
              cost = 6;
          }

          if (connectsToStaging)
            out << "  /* Base station */\n";
          out << "  " << _vertexData[i].id;
          if (_vertexData[i].id < 10)
            out <<  " ";
          out << " -- " << _vertexData[j].id;
          if (_vertexData[j].id < 10)
            out <<  " ";
          out << "  " << "[label=" << cost << "];\n";
        }
      }
    }
  }

  out << "}";
  std::cout << out.str() << std::endl;
}

/// \brief Main function to generate DOT from input sdf file
/// \param[in] _sdfFile Input sdf file.
void generateDOT(const std::string &_sdfFile, const std::string &_circuit)
{
  std::ifstream file(_sdfFile);
  if (!file.is_open())
  {
    std::cerr << "Failed to read file " << _sdfFile << std::endl;
    return;
  }
  std::string str((std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());

  // filter tiles that do not have connections
  std::function<bool(const std::string &, const std::string &)>
      filter = [](const std::string &/*_name*/,
      const std::string &_type)
  {
    return subt::ConnectionHelper::connectionPoints.count(_type) <= 0;
  };

  std::vector<VertexData> vertexData;
  while (!str.empty())
  {
    size_t result = std::string::npos;
    std::string includeStr = SdfParser::Parse("include", str, result);
    if (result == std::string::npos || result > str.size())
      break;

    VertexData vd;
    bool filled = SdfParser::FillVertexData(includeStr, vd, filter);
    if (filled)
      vertexData.push_back(vd);

    str = str.substr(result);
  }

  printGraph(vertexData, _circuit);

  file.close();
}

//////////////////////////////////////////////////
int main(int argc, char **argv)
{
  if (argc != 2 && argc != 3)
  {
    usage();
    return -1;
  }

  std::string circuit = "";
  std::string sdfFile = argv[1];

  if (argc == 2)
  {
    // Sanity check: --finals can't be used without the sdfFile argument.
    if (argv[1] == std::string("--finals"))
    {
      usage();
      return -1;
    }
  }

  if (argc == 3)
  {
    // Sanity check: One of the two arguments needs to be --finals.
    if (argv[1] != std::string("--finals") &&
        argv[2] != std::string("--finals"))
    {
      usage();
      return -1;
    }

    circuit = "--finals";
    if (argv[1] == std::string("--finals"))
      sdfFile = argv[2];
  }

  generateDOT(sdfFile, circuit);

  return 0;
}
