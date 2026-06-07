/*
 * constanttable.cpp - Direct3D 11 constant table.
 *
 * Copyright (c) 2021-2024 Jimmy Cassis
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pch.h"
#include "constanttable.h"

using namespace DirectX;

bool ShaderConstantBuffer::Create(ID3D11Device* pDevice)
{
    if (!pDevice || Description.Size == 0)
        return false;

    CD3D11_BUFFER_DESC desc(Description.Size, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    if (FAILED(pDevice->CreateBuffer(&desc, nullptr, &Data)))
        return false;

    return true;
}

bool ShaderConstantBuffer::HasChanges()
{
    for (auto var : Variables)
    {
        if (var.IsDirty)
            return true;
    }
    return false;
}

CConstantTable::CConstantTable(ID3D11ShaderReflection* pReflection) :
    m_iRefCount(1),
    m_pReflection(pReflection),
    MinimumFeatureLevel(D3D_FEATURE_LEVEL_9_1),
    ShaderDesc{}
{
}

CConstantTable::~CConstantTable()
{
    if (m_pReflection)
    {
        m_pReflection->Release();
        m_pReflection = nullptr;
    }

    for (size_t i = 0; i < m_ConstantBuffers.size(); i++)
    {
        SafeRelease(m_ConstantBuffers[i].Data);
        for (size_t j = 0; j < m_ConstantBuffers[i].Variables.size(); j++)
            delete[] static_cast<unsigned char*>(m_ConstantBuffers[i].Variables[j].Value);
    }
    m_ConstantBuffers.clear();
}

bool CConstantTable::GrabShaderData(ID3D11Device* pDevice)
{
    if (m_pReflection == nullptr || !pDevice)
        return false;

    m_pReflection->GetDesc(&ShaderDesc);
    m_pReflection->GetMinFeatureLevel(&MinimumFeatureLevel);

    for (UINT i = 0; i < ShaderDesc.ConstantBuffers; ++i)
    {
        ShaderConstantBuffer constantBuffer;
        ID3D11ShaderReflectionConstantBuffer* buffer = m_pReflection->GetConstantBufferByIndex(i);
        if (!buffer)
            return false;

        if (FAILED(buffer->GetDesc(&constantBuffer.Description)))
            return false;

        for (UINT v = 0; v < constantBuffer.Description.Variables; ++v)
        {
            ShaderVariable shaderVariable;

            ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
            if (!variable)
                return false;

            if (FAILED(variable->GetDesc(&shaderVariable.Description)))
                return false;

            ID3D11ShaderReflectionType* type = variable->GetType();
            if (!type)
                return false;

            if (FAILED(type->GetDesc(&shaderVariable.Type)))
                return false;

            constantBuffer.Variables.push_back(shaderVariable);
        }

        if (!constantBuffer.Create(pDevice))
            return false;

        m_ConstantBuffers.push_back(constantBuffer);
    }

    for (UINT i = 0; i < ShaderDesc.BoundResources; i++)
    {
        ShaderBinding shaderBinding{};
        m_pReflection->GetResourceBindingDesc(i, &shaderBinding.Description);
        m_Bindings.push_back(shaderBinding);
    }

    return true;
}

size_t CConstantTable::GetVariablesCount()
{
    size_t total = 0;
    for (size_t i = 0; i < m_ConstantBuffers.size(); i++)
        total += m_ConstantBuffers[i].Variables.size();

    return total;
}

void CConstantTable::GetBuffers(ID3D11Buffer** ppBuffers)
{
    if (!ppBuffers)
        return;

    for (size_t i = 0; i < m_ConstantBuffers.size(); ++i)
    {
        ppBuffers[i] = m_ConstantBuffers[i].Data;
    }
}

int CConstantTable::GetTextureSlot(std::string& strName)
{
    for (auto binding : m_Bindings)
    {
        if (binding.Description.Type == D3D_SIT_TEXTURE && binding.Description.Name && binding.Description.Name == strName)
            return binding.Description.BindPoint;
    }

    return -1;
}

ShaderVariable* CConstantTable::GetVariableByName(std::string& strName)
{
    for (size_t i = 0; i < m_ConstantBuffers.size(); i++)
    {
        for (size_t j = 0; j < m_ConstantBuffers[i].Variables.size(); j++)
        {
            if (m_ConstantBuffers[i].Variables[j].Description.Name && m_ConstantBuffers[i].Variables[j].Description.Name == strName)
                return &m_ConstantBuffers[i].Variables[j];
        }
    }
    return nullptr;
}

static bool StoreVariableValue(ShaderVariable* variable, const void* source, size_t sourceSize)
{
    if (!variable || !source || variable->Description.Size == 0)
        return false;

    const size_t destinationSize = variable->Description.Size;
    if (!variable->Value || variable->ValueSize != destinationSize)
    {
        delete[] static_cast<unsigned char*>(variable->Value);
        variable->Value = new (std::nothrow) unsigned char[destinationSize];
        variable->ValueSize = variable->Value ? destinationSize : 0;
    }

    if (!variable->Value)
        return false;

    memset(variable->Value, 0, destinationSize);
    memcpy(variable->Value, source, std::min(destinationSize, sourceSize));
    variable->IsDirty = true;
    return true;
}

bool CConstantTable::SetVector(LPCSTR handle, XMFLOAT4* vector)
{
    if (!handle || !vector)
        return false;

    std::string strName(handle);
    ShaderVariable* variable = GetVariableByName(strName);
    if (variable && variable->Type.Class == D3D_SVC_VECTOR)
        return StoreVariableValue(variable, vector, sizeof(*vector));

    return false;
}

bool CConstantTable::SetMatrix(LPCSTR handle, XMMATRIX* matrix)
{
    if (!handle || !matrix)
        return false;

    std::string strName(handle);
    ShaderVariable* variable = GetVariableByName(strName);
    if (variable && variable->Type.Class == D3D_SVC_MATRIX_COLUMNS)
    {
        XMMATRIX colums = XMMatrixTranspose(*matrix);
        XMFLOAT4X3 floats;
        XMStoreFloat4x3(&floats, colums);

        return StoreVariableValue(variable, floats.m, sizeof(floats.m));
    }

    return false;
}

bool CConstantTable::ApplyChanges(ID3D11DeviceContext* pContext)
{
    if (!pContext)
        return false;

    bool applied = false;
    for (size_t i = 0; i < m_ConstantBuffers.size(); i++)
    {
        if (!m_ConstantBuffers[i].HasChanges())
            continue;

        D3D11_MAPPED_SUBRESOURCE res;
        if (S_OK != pContext->Map(m_ConstantBuffers[i].Data, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
            continue;

        for (size_t j = 0; j < m_ConstantBuffers[i].Variables.size(); j++)
        {
            ShaderVariable* var = &m_ConstantBuffers[i].Variables[j];
            if (var->IsDirty && var->Value)
            {
                const size_t startOffset = var->Description.StartOffset;
                const size_t dataSize = var->Description.Size;
                const size_t bufferSize = m_ConstantBuffers[i].Description.Size;
                if (dataSize <= var->ValueSize && startOffset <= bufferSize && dataSize <= bufferSize - startOffset)
                {
                    memcpy(static_cast<unsigned char*>(res.pData) + startOffset, var->Value, dataSize);
                    applied = true;
                }
                var->IsDirty = false;
            }
        }

        pContext->Unmap(m_ConstantBuffers[i].Data, 0);
    }
    return applied;
}

ShaderVariable* CConstantTable::GetVariableByIndex(size_t index)
{
    for (size_t i = 0; i < m_ConstantBuffers.size(); i++)
    {
        if (index >= m_ConstantBuffers[i].Variables.size())
        {
            index -= m_ConstantBuffers[i].Variables.size();
            continue;
        }
        return &m_ConstantBuffers[i].Variables[index];
    }
    return nullptr;
}

ShaderBinding* CConstantTable::GetBindingByIndex(UINT index)
{
    if (index < m_Bindings.size())
        return &m_Bindings[index];

    return nullptr;
}
