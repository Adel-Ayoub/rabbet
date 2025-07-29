#include "rabbet/render/ModelLoader.h"

#include "rabbet/util/Log.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

namespace rb {
namespace {

glm::vec3 toVec3(const aiVector3D& v) {
    return {v.x, v.y, v.z};
}

glm::vec2 toUv(const aiVector3D& v) {
    return {v.x, v.y};
}

MeshData convertMesh(const aiMesh& mesh) {
    MeshData data;
    data.vertices.reserve(mesh.mNumVertices);
    for (unsigned int i = 0; i < mesh.mNumVertices; ++i) {
        Vertex vertex;
        vertex.position = toVec3(mesh.mVertices[i]);
        if (mesh.HasNormals()) {
            vertex.normal = toVec3(mesh.mNormals[i]);
        }
        if (mesh.HasTextureCoords(0)) {
            vertex.uv = toUv(mesh.mTextureCoords[0][i]);
        }
        data.vertices.push_back(vertex);
    }

    data.indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3u);
    for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
        const aiFace& face = mesh.mFaces[f];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            data.indices.push_back(face.mIndices[j]);
        }
    }
    return data;
}

ModelMaterial convertMaterial(const aiMaterial& material) {
    ModelMaterial result;
    aiColor3D color{1.0f, 1.0f, 1.0f};
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        result.baseColor = {color.r, color.g, color.b};
    }
    aiString texturePath;
    if (material.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS) {
        result.baseColorTexture = texturePath.C_Str();
    }
    return result;
}

} // namespace

std::optional<Model> loadModel(const std::filesystem::path& path) {
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                               aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace;
    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr) {
        log::error("failed to load model '{}': {}", path.string(), importer.GetErrorString());
        return std::nullopt;
    }

    Model model;
    model.materials.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        model.materials.push_back(convertMaterial(*scene->mMaterials[i]));
    }

    model.meshes.reserve(scene->mNumMeshes);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh& mesh = *scene->mMeshes[i];
        ModelMesh modelMesh;
        modelMesh.data = convertMesh(mesh);
        modelMesh.materialIndex = static_cast<int>(mesh.mMaterialIndex);
        model.meshes.push_back(std::move(modelMesh));
    }

    log::info("loaded model '{}': {} mesh(es), {} material(s)", path.string(), model.meshes.size(),
              model.materials.size());
    return model;
}

} // namespace rb
