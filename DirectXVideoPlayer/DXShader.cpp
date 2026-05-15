#include "DXShader.h"
#include <iostream>
#include <d3dcompiler.h>
#include <wrl/client.h>

DXShader::~DXShader() {
	if (vs) 
		vs->Release();
	if (ps) 
		ps->Release();
	if (layout) 
		layout->Release();
}

bool DXShader::LoadFromFile(ID3D11Device* device, const std::wstring& filename) 
{
	if (!CompileVertexShader(device, filename))
		return false;
	if (!CompilePixelShader(device, filename))
		return false;
	return true;
}

bool DXShader::CompileVertexShader(ID3D11Device* device, const std::wstring& filename)
{
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", 0, 0, &vsBlob, &errorBlob
    );

    if (FAILED(hr))
    {
        OutputCompileErrors(errorBlob.Get(), filename, L"Vertex Shader");
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);

    if (FAILED(hr))
    {
        std::cerr << "Failed to create vertex shader from file: " << filename.c_str() << std::endl;
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);

    if (FAILED(hr))
    {
        std::cerr << "Failed to create input layout from file: " << filename.c_str() << std::endl;
        return false;
    }

    return SUCCEEDED(hr);
}

bool DXShader::CompilePixelShader(ID3D11Device* device, const std::wstring& filename)
{
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", 0, 0, &psBlob, &errorBlob
    );

    if (FAILED(hr))
    {
        OutputCompileErrors(errorBlob.Get(), filename, L"Pixel Shader");
        return false;
    }

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);

    return SUCCEEDED(hr);
}

void DXShader::OutputCompileErrors(ID3DBlob* errorBlob, const std::wstring& filename, const std::wstring& shaderType)
{
    if (errorBlob)
    {
        char* compileErrors = (char*)(errorBlob->GetBufferPointer());
        std::wcerr << "Error compiling " << shaderType.c_str() << " shader from file " << filename.c_str() << ":\n" << compileErrors << std::endl;
    }
    else
    {
        std::wcerr << "Could not find or open shader file: " << filename.c_str() << std::endl;
    }
}