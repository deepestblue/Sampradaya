#pragma warning(disable: 5045)

#include <string>
#include <fstream>
#include <sstream>
#include <exception>
#include <iomanip>
#include <vector>
#include <limits>
#include <iostream>

#pragma warning(default: 5045)

#define NOMINMAX

#include <comdef.h>
#include <wrl/client.h>
#include <d2d1_1.h>
#include <dwrite_3.h>
#include <wincodec.h>

//#define DEBUG

using namespace std;
using Microsoft::WRL::ComPtr;
using D2D1::ColorF;
using D2D1::RectF;
using D2D1::PixelFormat;
using D2D1::Matrix3x2F;

const float typeface_size_pt = 48.f;
const wstring typeface_file_path = L"../../src/windows/Sampradaya.ttf"s;

template <typename E>
void
throw_if_failed(
    bool exp,
    const E &e
) {
    if (exp)
        return;

    throw runtime_error(e());
}

void
throw_if_failed(int win32_return_code) {
    auto win32_error_msg = [] {
        auto last_error = GetLastError();
        auto buffer = unique_ptr<char, decltype(&LocalFree)>(
            nullptr,
            LocalFree
        );
        auto len = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            last_error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer.get(),
            0,
            nullptr
        );

        return string{buffer.get(), len};
    };

    throw_if_failed(
        win32_return_code > 0,
        win32_error_msg
    );
}

wstring
utf8_to_utf16(
    const string &in
) {
    auto buf_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        in.c_str(),
        static_cast<int>(in.length()),
        nullptr,
        0
    );
    throw_if_failed(buf_size);

    auto out = wstring(static_cast<size_t>(buf_size), 0);
    throw_if_failed(
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            in.c_str(),
            static_cast<int>(in.length()),
            out.data(),
            buf_size
        )
    );
    return out;
}

string
utf16_to_utf8(
    const wstring &in
) {
    auto buf_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        in.c_str(),
        static_cast<int>(in.length()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    throw_if_failed(buf_size);

    auto out = string(static_cast<size_t>(buf_size), 0);
    throw_if_failed(
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            in.c_str(),
            static_cast<int>(in.length()),
            out.data(),
            buf_size,
            nullptr,
            nullptr
        )
    );

    return out;
}

void
throw_if_failed(HRESULT hr) {
    struct com_error_msg {
        com_error_msg(HRESULT hr)
        : hresult(hr) {}

        string
        operator() () const {
            _com_error com_error(hresult);
            auto s = wstringstream{};
            s << L"Failure with HRESULT of 0x"s;
            s << setfill(L'0') << setw(sizeof(HRESULT) * 2) // 2 hex digits per char
                << hex << static_cast<unsigned int>(hresult);
            s << setw(0) << L" ("s << com_error.ErrorMessage() << L" )\n"s;

            return utf16_to_utf8(s.str());
        }
    private:
        HRESULT hresult;
    };

    throw_if_failed(
        SUCCEEDED(hr),
        com_error_msg(hr)
    );
}

class COM_initer {
public:
    COM_initer() {
        throw_if_failed(
            CoInitializeEx(
                nullptr,
                COINIT_MULTITHREADED
            )
        );
    }
    ~COM_initer() {
        CoUninitialize();
    }
};

pair<ComPtr<IDWriteFontCollection>, wstring>
create_font_collection(
    const ComPtr<IDWriteFactory5> &dwrite_factory
) {
    ComPtr<IDWriteFontSetBuilder1> font_set_builder;
    throw_if_failed(
        dwrite_factory->CreateFontSetBuilder(&font_set_builder)
    );

    ComPtr<IDWriteFontFile> font_file;
    throw_if_failed(
        dwrite_factory->CreateFontFileReference(
            typeface_file_path.c_str(),
            nullptr,
            &font_file
        )
    );

    throw_if_failed(
        font_set_builder->AddFontFile(font_file.Get())
    );

    ComPtr<IDWriteFontSet> font_set;
    throw_if_failed(
        font_set_builder->CreateFontSet(&font_set)
    );

    ComPtr<IDWriteFontCollection1> font_collection;
    throw_if_failed(
        dwrite_factory->CreateFontCollectionFromFontSet(
            font_set.Get(),
            &font_collection
        )
    );

    ComPtr<IDWriteFontFamily> font_family;
    throw_if_failed(
        font_collection->GetFontFamily(
            0,
            &font_family
        )
    );

    ComPtr<IDWriteLocalizedStrings> family_names;
    throw_if_failed(
        font_family->GetFamilyNames(&family_names)
    );

    auto count = family_names->GetCount();
    throw_if_failed(
        count > 0,
        [] { return "Typeface has no names."s; }
    );

    auto buf_size = unsigned int{};
    throw_if_failed(
        family_names->GetStringLength(
            0,
            &buf_size
        )
    );
    ++buf_size;

    wstring typeface_name(buf_size, 0);
    throw_if_failed(
        family_names->GetString(
            0,
            typeface_name.data(),
            buf_size
        )
    );

    return {font_collection, typeface_name};
}

ComPtr<IDWriteTextLayout>
create_dwrite_text_layout(
    const ComPtr<IDWriteFactory5> &dwrite_factory,
    const ComPtr<IDWriteTextFormat> &text_format,
    const string &text
) {
    auto utf16_text = utf8_to_utf16(text);

    ComPtr<IDWriteTextLayout> dwrite_text_layout;
    throw_if_failed(
        dwrite_factory->CreateTextLayout(
            utf16_text.c_str(),
            static_cast<uint32_t>(utf16_text.length()),
            text_format.Get(),
            numeric_limits<float>::max(),
            numeric_limits<float>::max(),
            &dwrite_text_layout
        )
    );

#ifdef DEBUG
    DWRITE_TEXT_METRICS metrics;
    throw_if_failed(
        dwrite_text_layout->GetMetrics(
            &metrics
        )
    );

    wcout << L"Metrics for "s << text.c_str() << L" are as follows."s << L'\n';
    wcout << L"Width: "s << metrics.width << L'\n';
    wcout << L"Height: "s << metrics.height << L'\n';
#endif

    return dwrite_text_layout;
}

ComPtr<ID2D1RenderTarget>
create_render_target(
    const ComPtr<ID2D1Factory1> &d2d_factory,
    const ComPtr<IWICBitmap> &wic_bitmap
) {
    ComPtr<ID2D1RenderTarget> render_target;

    auto pixel_format = PixelFormat(
        DXGI_FORMAT_UNKNOWN,
        D2D1_ALPHA_MODE_IGNORE
    );
    auto render_props = D2D1_RENDER_TARGET_PROPERTIES{
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        pixel_format,
        0,
        0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT,
    };
    throw_if_failed(
        d2d_factory->CreateWicBitmapRenderTarget(
            wic_bitmap.Get(),
            &render_props,
            &render_target
        )
    );

    render_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE::D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    return render_target;
}

void
encode_wicbitmap_onto_wicstream(
    const ComPtr<IWICImagingFactory2> &wic_factory,
    IWICStream *stream,
    const ComPtr<IWICBitmap> &wic_bitmap
) {
    auto wic_bitmap_encoder = static_cast<IWICBitmapEncoder *>(nullptr);
    throw_if_failed(
        wic_factory->CreateEncoder(
            GUID_ContainerFormatBmp,
            nullptr,
            &wic_bitmap_encoder
        )
    );

    throw_if_failed(
        wic_bitmap_encoder->Initialize(
            stream,
            WICBitmapEncoderNoCache
        )
    );

    {
        auto wic_frame_encode = static_cast<IWICBitmapFrameEncode *>(nullptr);
        throw_if_failed(
            wic_bitmap_encoder->CreateNewFrame(
                &wic_frame_encode,
                nullptr
            )
        );
        throw_if_failed(
            wic_frame_encode->Initialize(nullptr)
        );
        throw_if_failed(
            wic_frame_encode->WriteSource(
                wic_bitmap.Get(),
                nullptr
            )
        );
        throw_if_failed(
            wic_frame_encode->Commit()
        );
    }

    throw_if_failed(
        wic_bitmap_encoder->Commit()
    );
}

class Text_to_image_renderer {
public:
    Text_to_image_renderer()
    {
        throw_if_failed(
            CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wic_factory)
            )
        );

        auto options = D2D1_FACTORY_OPTIONS{};
        throw_if_failed(
            D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(d2d_factory),
                &options,
                &d2d_factory
            )
        );

        {
            throw_if_failed(
                DWriteCreateFactory(
                    DWRITE_FACTORY_TYPE_SHARED,
                    __uuidof(dwrite_factory),
                    &dwrite_factory
                )
            );

            auto [font_collection, typeface_name] = create_font_collection(
                dwrite_factory
            );

            throw_if_failed(
                dwrite_factory->CreateTextFormat(
                    typeface_name.c_str(),
                    font_collection.Get(),
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    typeface_size_pt,
                    L"",
                    &text_format
                )
            );
        }
    }

    void
    operator ()(
        const string &text,
        const wstring &output_filename
    ) {
        auto dwrite_text_layout = create_dwrite_text_layout(
            dwrite_factory,
            text_format,
            text
        );

        DWRITE_TEXT_METRICS metrics;
        throw_if_failed(
            dwrite_text_layout->GetMetrics(
                &metrics
            )
        );

        ComPtr<IWICBitmap> wic_bitmap;
        throw_if_failed(
            wic_factory->CreateBitmap(
                static_cast<unsigned int>(metrics.width),
                static_cast<unsigned int>(metrics.height),
                GUID_WICPixelFormat32bppBGR,
                WICBitmapCacheOnDemand,
                &wic_bitmap
            )
        );

        auto render_target = create_render_target(
            d2d_factory,
            wic_bitmap
        );

        render_target->BeginDraw();
        render_target->Clear(
            ColorF(ColorF::White)
        );

        ComPtr<ID2D1SolidColorBrush> black_brush;
        throw_if_failed(
            render_target->CreateSolidColorBrush(
                ColorF(ColorF::Black),
                &black_brush
            )
        );
        render_target->DrawTextLayout(
            D2D1_POINT_2F{},
            dwrite_text_layout.Get(),
            black_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_NONE
        );

        throw_if_failed(
            render_target->EndDraw()
        );

        auto stream = static_cast<IWICStream *>(nullptr);
        throw_if_failed(
            wic_factory->CreateStream(&stream)
        );
        throw_if_failed(
            stream->InitializeFromFilename(
                output_filename.c_str(),
                GENERIC_WRITE
            )
        );

        encode_wicbitmap_onto_wicstream(
            wic_factory,
            stream,
            wic_bitmap
        );
    }

private:
    ComPtr<IWICImagingFactory2> wic_factory;
    ComPtr<IDWriteTextFormat> text_format;
    ComPtr<IDWriteFactory5> dwrite_factory;
    ComPtr<ID2D1Factory1> d2d_factory;
};

int
wmain(
    int argc,
    wchar_t *argv[]
) try {
    throw_if_failed(
        argc == 3,
        [] { return "Need 3 arguments."s; }
    );
    const auto input_file = wstring{argv[1]};
    const auto output_dir = wstring{argv[2]};

    auto com_initer = COM_initer{};

    auto renderer = Text_to_image_renderer{};

    auto input_stream = ifstream{input_file};
    auto line = string{};

    //
    // Starting at one, because technically 0 is a zero-digit number, so ostreaming a '0'
    // should be the empty-string, but it's not, so the output filename for the zeroth
    // file looks wrong.
    //
    auto i = 1u;
    while (getline(input_stream, line)) {
        if (line.empty())
            continue;
        renderer(line, output_dir + L"/"s + to_wstring(i) + L".bmp"s);
        ++i;
    }
}
catch (const exception &e) {
    wcerr << L"Exception thrown: "s << e.what() << L'\n';
}
#pragma warning(disable: 4571)
catch (...) {
#pragma warning(default: 4571)
    wcerr << L"Something else thrown"s << L'\n';
}
