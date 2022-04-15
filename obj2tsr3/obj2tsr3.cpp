#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

using std::literals::string_literals::operator""s;

// Vector type
template<size_t size>
class Vec {
    float value[size];
public:
    Vec() {}

    Vec(const std::array<float, size>& i_value) {
        for (size_t i = 0; i < size; i++)
            value[i] = i_value[i];
    }

    float& operator[](const size_t i) {
        return value[i];
    }

    const float& operator[](const size_t i) const {
        return value[i];
    }

    bool operator==(const Vec<size>& rhs) const {
        for (size_t i = 0; i < size; i++)
            if (value[i] != rhs[i]) return false;

        return true;
    }
};

// IA8 / IA3 generator
template<size_t size>
struct IndexedArray {
    std::vector<Vec<size>> out_vertices;
    std::vector<size_t> out_indices;

    void OutVertex(const Vec<size>& value) {
        auto it = std::find(out_vertices.begin(), out_vertices.end(), value);

        if (it == out_vertices.end()) {
            out_vertices.push_back(value);
            out_indices.push_back(out_vertices.size() - 1);
        } else {
            int index = it - out_vertices.begin();
            out_indices.push_back(index);
        }
    }
};

// OBJ / MTL parser
void ParseFile(fs::path file_path, std::function<void(const std::string& command, std::istringstream& ls)> callback) {
    std::ifstream ifs(file_path);

    if (!ifs.good()) throw std::runtime_error("Cannot open \""s + file_path.string() + "\""s);

    for (std::string line; std::getline(ifs, line); ) {
        if (line[0] == '#' || line.empty()) continue;

        std::istringstream ls(line);

        std::string command;
        ls >> command >> std::ws;

        callback(command, ls);
    }
}

template<typename T>
void PutBytes(std::ostream& stream, T value) {
    auto valueptr = (uint8_t*)(&value);
    for (size_t i = 0; i < sizeof(T); i++)
        stream << valueptr[i];
}

template<size_t size>
void CreateIA(fs::path path, const IndexedArray<size>& data) {
    std::ofstream ofs(path, std::ofstream::binary);

    if (!ofs.good()) throw std::runtime_error("Cant output file");

    // Magic number
    ofs << "IA" << ((char)(size + '0')) << '\0';

    // Reserved
    for (size_t i = 0; i < 12; i++) ofs << '\0';

    // Vertices block
    PutBytes<uint32_t>(ofs, data.out_vertices.size());

    for (const auto& vertex : data.out_vertices) {
        for (size_t i = 0; i < size; i++)
            PutBytes(ofs, vertex[i]);
    }

    // Indices block
    PutBytes<uint32_t>(ofs, data.out_indices.size());

    for (const auto index : data.out_indices)
        PutBytes<uint32_t>(ofs, index);

}

int main(int argc, char* argv[]) {
    try {
        printf("OBJ2TSR3 | OBJ to TSR3 Files Converter\n======================================\n");

        if (argc < 2) throw std::invalid_argument("Too few arguments\nUsage: obj2tsr3 <obj file name>");

        std::string obj_name = argv[1];
        fs::path obj_path(obj_name);
        fs::path obj_dir_path = fs::absolute(obj_path).parent_path();
        fs::path current_path = fs::absolute(fs::current_path());
        fs::path obj_stem = obj_path.stem();
        fs::path obj_data_path = fs::absolute(current_path / obj_stem);

        auto DumpPath = [](const std::string& desc, const fs::path& path) {
            printf("%-20s \"%s\"\n", desc.c_str(), path.string().c_str());
        };

        DumpPath("Source file:", obj_path);
        DumpPath("Source directory:", obj_dir_path);
        DumpPath("Export directory:", current_path);
        DumpPath("Data directory:", obj_data_path);

        printf("\n");

        std::vector<Vec<3>> positions;
        std::vector<Vec<2>> uvs;
        std::vector<Vec<3>> normals;

        std::map<std::string, std::string> material_textures;
        std::map<std::string, IndexedArray<8>> materials;
        IndexedArray<8>* current_material = nullptr;

        IndexedArray<3> collision_mesh;

        // Parse obj
        ParseFile(obj_path, [&](const std::string& command, std::istringstream& ls) {
            if (command == "mtllib") {
                std::string mtl_name;
                std::getline(ls, mtl_name);

                fs::path mtl_path(mtl_name);
                mtl_path = obj_dir_path / mtl_path;

                DumpPath("MtlLib", mtl_path);

                std::string current_material_name;

                // Parse mtl
                ParseFile(mtl_path, [&](const std::string& command, std::istringstream& ls) {
                    if (command == "newmtl")
                        std::getline(ls, current_material_name);
                    else if (command == "map_Kd")
                        std::getline(ls, material_textures[current_material_name]);
                });
            } else if (command == "usemtl") {
                std::string material_name;
                std::getline(ls, material_name);
                current_material = &materials[material_name];

                printf("Compiling material \"%s\"\n", material_name.c_str());
            } else if (command == "v") {
                Vec<3> v;
                ls >> v[0] >> v[1] >> v[2];
                positions.push_back(v);
            } else if (command == "vt") {
                Vec<2> vt;
                ls >> vt[0] >> vt[1];
                uvs.push_back(vt);
            } else if (command == "vn") {
                Vec<3> vn;
                ls >> vn[0] >> vn[1] >> vn[2];
                normals.push_back(vn);
            } else if (command == "f") {
                if (!current_material) throw std::runtime_error("F but no material");

                for (size_t i = 0; i < 3; i++) {
                    char dummy;
                    size_t indices[3];
                    ls >> indices[0] >> dummy >> indices[1] >> dummy >> indices[2];

                    if (indices[0] > positions.size()) throw std::out_of_range("Position out of range");
                    if (indices[1] > uvs.size()) throw std::out_of_range("UV out of range");
                    if (indices[2] > normals.size()) throw std::out_of_range("Normal out of range");

                    auto& position = positions[indices[0] - 1];
                    auto& uv = uvs[indices[1] - 1];
                    auto& normal = normals[indices[2] - 1];

                    current_material->OutVertex({ { position[0], position[1], position[2], uv[0], uv[1], normal[0], normal[1], normal[2] } });
                    collision_mesh.OutVertex({ { position[0], position[1], position[2] } });
                }
            }
        });

        printf("\nExporting...\n\n");

        if (!fs::is_directory(obj_data_path))
            fs::create_directory(obj_data_path);

        // Graphics export

        for (const auto& material : materials) {
            fs::path material_ia8(obj_data_path / fs::path(material.first + ".ia8"));
            DumpPath("Export: ", material_ia8);

            size_t num_vertices = material.second.out_vertices.size();
            size_t num_indices = material.second.out_indices.size();
            printf("%u vertices, %u indices (each vertex used %.1f times in avg)\n\n", num_vertices, num_indices, (float)num_indices / (float)num_vertices);

            CreateIA<8>(material_ia8, material.second);
        }

        // Physics export

        fs::path ia3(obj_data_path / fs::path("collision.ia3"));
        DumpPath("Collision: ", ia3);

        size_t num_vertices = collision_mesh.out_vertices.size();
        size_t num_indices = collision_mesh.out_indices.size();
        printf("%u vertices, %u indices (each vertex used %.1f times in avg)\n\n", num_vertices, num_indices, (float)num_indices / (float)num_vertices);

        CreateIA<3>(ia3, collision_mesh);

        // TMDL export

        printf("\nExporting TMDL...\n\n");

        nlohmann::json tmdl;

        std::string model_name = obj_stem.string();
        auto tmdl_path = current_path / fs::path(model_name + ".tmdl"s);

        // Read current tmdl
        if (fs::is_regular_file(tmdl_path)) {
            std::ifstream tmdl_i(tmdl_path);
            if (!tmdl_i.good()) throw std::runtime_error("Cannot open TMDL \""s + tmdl_path.string() + "\""s);
            tmdl_i >> tmdl;
            tmdl_i.close();
        }

        auto& tmdl_draw = tmdl["draw"];
        for (const auto& material : material_textures) {
            auto& tmdl_material = tmdl_draw[material.first];
            tmdl_material["mesh"] = model_name + "/"s + material.first + ".ia8"s;
            tmdl_material["texture"] = std::regex_replace(material.second, std::regex("\\\\\\\\"), "/");
        }

        if (!tmdl.contains("name"))
            tmdl["name"] = model_name;

        if (!tmdl.contains("collision"))
            tmdl["collision"] = model_name + "/collision.ia3"s;

        if (!tmdl.contains("mass"))
            tmdl["mass"] = 0.0f;

        std::ofstream tmdl_o(tmdl_path);
        if (!tmdl_o.good()) throw std::runtime_error("Cannot open TMDL \""s + tmdl_path.string() + "\" for output"s);
        tmdl_o << std::setw(4) << tmdl;
        tmdl_o.close();

        printf("\nCompleted.\n\n");

    } catch (std::exception ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;

}
