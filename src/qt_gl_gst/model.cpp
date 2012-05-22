

#include "model.h"



Model::Model()
{
    scene = NULL;

    struct aiLogStream stream = aiGetPredefinedLogStream(aiDefaultLogStream_STDOUT, NULL);
    aiAttachLogStream(&stream);

    //stream = aiGetPredefinedLogStream(aiDefaultLogStream_FILE,"assimp_log.txt");
    //aiAttachLogStream(&stream);

}

Model::~Model()
{
    nodes.resize(0);

    if(scene)
    {
        aiReleaseImport(scene);
        scene = NULL;
    }

    aiDetachAllLogStreams();
}

void Model::aiNodesToVertexArrays()
{
    /* Depth first traverse node tree and place nodes in flat QList,
       then work on each node in QList to create usable arrays.

       Each node in tree has meshes, each mesh has faces, each face has indices
       one indice is set of co-ordinates/tex co-ords/colour/normal for one point (in a polygon say)

       Transformation is per node,
       Texture/material is per mesh, so we want 1 set of arrays for each mesh

       Get all the points out and put them in:
       QVector of nodes
           Transformation matrix ptr
           QVector of meshes
               texid
               vertices: QVector of QVector3D
               tex co-ords: QVector of QVector3D
               normals: QVector of QVector3D

       Only bother with triangles and see how that turns out.

    */

    QList<struct aiNode*> flatNodePtrList;
    struct aiNode* currentNode = scene->mRootNode;
    flatNodePtrList.prepend(currentNode);

    while(flatNodePtrList.size())
    {
        // Store children nodes to process next, removing the
        // current (parent) node from the front of the list:
        currentNode = flatNodePtrList.takeFirst();
        for(int childNodeIx = currentNode->mNumChildren-1; childNodeIx >= 0; --childNodeIx)
        {
            flatNodePtrList.prepend(currentNode->mChildren[childNodeIx]);
        }

        // Process the current node:
        ModelNode newModelNode;

        newModelNode.transformMatrix = QMatrix4x4((qreal)currentNode->mTransformation.a1,
                                                  (qreal)currentNode->mTransformation.a2,
                                                  (qreal)currentNode->mTransformation.a3,
                                                  (qreal)currentNode->mTransformation.a4,
                                                  (qreal)currentNode->mTransformation.b1,
                                                  (qreal)currentNode->mTransformation.b2,
                                                  (qreal)currentNode->mTransformation.b3,
                                                  (qreal)currentNode->mTransformation.b4,
                                                  (qreal)currentNode->mTransformation.c1,
                                                  (qreal)currentNode->mTransformation.c2,
                                                  (qreal)currentNode->mTransformation.c3,
                                                  (qreal)currentNode->mTransformation.c4,
                                                  (qreal)currentNode->mTransformation.d1,
                                                  (qreal)currentNode->mTransformation.d2,
                                                  (qreal)currentNode->mTransformation.d3,
                                                  (qreal)currentNode->mTransformation.d4);


        for(unsigned int meshIx = 0; meshIx < currentNode->mNumMeshes; ++meshIx)
        {
            const struct aiMesh* currentMesh = scene->mMeshes[currentNode->mMeshes[meshIx]];

            ModelMesh newModelMesh;

            // TODO: Grab texture info/load image file here....

            newModelMesh.hasNormals = currentMesh->HasNormals();
            newModelMesh.hasTexcoords = currentMesh->HasTextureCoords(0);

            for(unsigned int faceIx = 0; faceIx < currentMesh->mNumFaces; ++faceIx)
            {
                const struct aiFace* currentFace = &currentMesh->mFaces[faceIx];

                if(currentFace->mNumIndices != 3)
                {
                    qDebug ("Ignoring non-triangle mesh %d face %d\n", meshIx, faceIx);
                }


                for(unsigned int i = 0; i < currentFace->mNumIndices; i++)
                {
                    int vertexIndex = currentFace->mIndices[i];

                    QVector3D vert(currentMesh->mVertices[vertexIndex].x, currentMesh->mVertices[vertexIndex].y, currentMesh->mVertices[vertexIndex].z);
                    newModelMesh.triangleVertices.append(vert);

                    if(newModelMesh.hasNormals)
                    {
                        QVector3D norm(currentMesh->mNormals[vertexIndex].x, currentMesh->mNormals[vertexIndex].y, currentMesh->mNormals[vertexIndex].z);
                        newModelMesh.triangleNormals.append(norm);
                    }

                    if(newModelMesh.hasTexcoords)
                    {
                        QVector2D tex(currentMesh->mTextureCoords[0][vertexIndex].x, 1 - currentMesh->mTextureCoords[0][vertexIndex].y);
                        newModelMesh.triangleTexcoords.append(tex);
                    }

                }
            }

            newModelNode.meshes.append(newModelMesh);
        }

        nodes.append(newModelNode);
    }
}


int Model::Load(QString fileName)
{
    if(scene)
    {
        // Clear extracted node data
        nodes.resize(0);

        aiReleaseImport(scene);
        scene = NULL;
    }

    // Load model
    scene = aiImportFile(fileName.toAscii().constData(), aiProcessPreset_TargetRealtime_Quality);

    if (!scene)
    {
        qCritical() << "Couldn't load obj model file " << fileName;
        return -1;
    }

    // Extract from ai mesh/faces into arrays
    aiNodesToVertexArrays();

    // Get the offset to center the model about the origin when drawing later
    get_bounding_box(&scene_min,&scene_max);
    scene_center.x = (scene_min.x + scene_max.x) / 2.0f;
    scene_center.y = (scene_min.y + scene_max.y) / 2.0f;
    scene_center.z = (scene_min.z + scene_max.z) / 2.0f;

    // Sensible default
    scaleFactor = 1.0;

    return 0;
}

void Model::SetScale(qreal boundarySize)
{
    if (!scene)
    {
        qCritical() << "Model file not loaded yet";
        return;
    }

    float longestSide = scene_max.x-scene_min.x;
    longestSide = qMax(scene_max.y - scene_min.y, longestSide);
    longestSide = qMax(scene_max.z - scene_min.z, longestSide);

    scaleFactor = boundarySize / (qreal)longestSide;
}

void Model::Draw(QMatrix4x4 modelViewMatrix, QMatrix4x4 projectionMatrix, QGLShaderProgram *shaderProg, bool useModelTextures)
{
    if (!scene)
    {
        qCritical() << "Model file not loaded yet";
        return;
    }

    // Center and scale the model
    modelViewMatrix.scale(scaleFactor);
    modelViewMatrix.translate(-scene_center.x, -scene_center.y, -scene_center.z);

    foreach(ModelNode node, nodes)
    {
        QMatrix4x4 nodeModelViewMatrix = modelViewMatrix * node.transformMatrix;

        // Load modelview projection matrix into shader. The projection matrix must
        // be multiplied by the modelview, not the other way round!
        shaderProg->setUniformValue("u_mvp_matrix", projectionMatrix * nodeModelViewMatrix);
        shaderProg->setUniformValue("u_mv_matrix", nodeModelViewMatrix);

        foreach(ModelMesh mesh, node.meshes)
        {
            if(useModelTextures)
            {
                // Set/enable texture id if desired ....
            }

            if(mesh.hasNormals)
            {
                shaderProg->enableAttributeArray("a_normal");
                shaderProg->setAttributeArray("a_normal", mesh.triangleNormals.constData());
            }

            if(mesh.hasTexcoords)
            {
                shaderProg->enableAttributeArray("a_texCoord");
                shaderProg->setAttributeArray("a_texCoord", mesh.triangleTexcoords.constData());
            }

            shaderProg->enableAttributeArray("a_vertex");
            shaderProg->setAttributeArray("a_vertex", mesh.triangleVertices.constData());

            glDrawArrays(GL_TRIANGLES, 0, mesh.triangleVertices.size());
            shaderProg->disableAttributeArray("a_vertex");
            shaderProg->disableAttributeArray("a_normal");
            shaderProg->disableAttributeArray("a_texCoord");
        }
    }
}


void Model::get_bounding_box_for_node (const struct aiNode* nd,
        struct aiVector3D* min,
        struct aiVector3D* max,
        struct aiMatrix4x4* trafo)
{
        struct aiMatrix4x4 prev;
        unsigned int n = 0, t;

        prev = *trafo;
        aiMultiplyMatrix4(trafo,&nd->mTransformation);

        for (; n < nd->mNumMeshes; ++n) {
                const struct aiMesh* mesh = scene->mMeshes[nd->mMeshes[n]];
                for (t = 0; t < mesh->mNumVertices; ++t) {

                        struct aiVector3D tmp = mesh->mVertices[t];
                        aiTransformVecByMatrix4(&tmp,trafo);

                        min->x = qMin(min->x,tmp.x);
                        min->y = qMin(min->y,tmp.y);
                        min->z = qMin(min->z,tmp.z);

                        max->x = qMax(max->x,tmp.x);
                        max->y = qMax(max->y,tmp.y);
                        max->z = qMax(max->z,tmp.z);
                }
        }

        for (n = 0; n < nd->mNumChildren; ++n) {
                get_bounding_box_for_node(nd->mChildren[n],min,max,trafo);
        }
        *trafo = prev;
}

void Model::get_bounding_box (struct aiVector3D* min, struct aiVector3D* max)
{
        struct aiMatrix4x4 trafo;
        aiIdentityMatrix4(&trafo);

        min->x = min->y = min->z =  1e10f;
        max->x = max->y = max->z = -1e10f;
        get_bounding_box_for_node(scene->mRootNode,min,max,&trafo);
}

