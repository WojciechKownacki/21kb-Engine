#include "private/ui/ScreenUIFontMSDFGenerator.hpp"
#include "private/ui/ScreenUIFontDistanceField.hpp"
#include <hb.h>
#include <hb-ot.h>
#include <msdfgen.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::render {
namespace {
struct Outline {
    msdfgen::Shape shape;
    msdfgen::Contour* contour=nullptr;
    msdfgen::Point2 first{},current{};
    void Close(){if(contour&&current!=first) contour->addEdge(msdfgen::EdgeHolder{current,first});contour=nullptr;}
    static void Move(hb_draw_funcs_t*,void* data,hb_draw_state_t*,float x,float y,void*) {auto& o=*static_cast<Outline*>(data);o.Close();o.contour=&o.shape.addContour();o.first=o.current={x,y};}
    static void Line(hb_draw_funcs_t*,void* data,hb_draw_state_t*,float x,float y,void*) {auto& o=*static_cast<Outline*>(data);if(!o.contour) return;const msdfgen::Point2 p{x,y};if(p!=o.current)o.contour->addEdge(msdfgen::EdgeHolder{o.current,p});o.current=p;}
    static void Quadratic(hb_draw_funcs_t*,void* data,hb_draw_state_t*,float cx,float cy,float x,float y,void*) {auto& o=*static_cast<Outline*>(data);if(!o.contour)return;const msdfgen::Point2 p{x,y};o.contour->addEdge(msdfgen::EdgeHolder{o.current,{cx,cy},p});o.current=p;}
    static void Cubic(hb_draw_funcs_t*,void* data,hb_draw_state_t*,float ax,float ay,float bx,float by,float x,float y,void*) {auto& o=*static_cast<Outline*>(data);if(!o.contour)return;const msdfgen::Point2 p{x,y};o.contour->addEdge(msdfgen::EdgeHolder{o.current,{ax,ay},{bx,by},p});o.current=p;}
    static void Finish(hb_draw_funcs_t*,void* data,hb_draw_state_t*,void*) {static_cast<Outline*>(data)->Close();}
};
}
struct ScreenUIFontMSDFGenerator::Impl {
    hb_blob_t* blob=nullptr;hb_face_t* face=nullptr;hb_font_t* font=nullptr;hb_draw_funcs_t* draw=nullptr;
    ~Impl(){if(draw)hb_draw_funcs_destroy(draw);if(font)hb_font_destroy(font);if(face)hb_face_destroy(face);if(blob)hb_blob_destroy(blob);}
};
ScreenUIFontMSDFGenerator::ScreenUIFontMSDFGenerator(std::span<const std::byte> bytes,std::span<const kb::scene::UITextFontVariation> variations) {
    if(bytes.empty()||bytes.size()>std::numeric_limits<unsigned>::max()) return;
    auto p=std::make_unique<Impl>();p->blob=hb_blob_create(reinterpret_cast<const char*>(bytes.data()),static_cast<unsigned>(bytes.size()),HB_MEMORY_MODE_READONLY,nullptr,nullptr);
    p->face=hb_face_create(p->blob,0);if(!hb_face_get_glyph_count(p->face))return;
    p->font=hb_font_create(p->face);hb_ot_font_set_funcs(p->font);const auto units=hb_face_get_upem(p->face);if(!units||units>65535)return;hb_font_set_scale(p->font,static_cast<int>(units),static_cast<int>(units));
    std::vector<hb_variation_t> axes;for(const auto axis:variations)axes.push_back({axis.tag,axis.value});if(!axes.empty())hb_font_set_variations(p->font,axes.data(),static_cast<unsigned>(axes.size()));
    p->draw=hb_draw_funcs_create();hb_draw_funcs_set_move_to_func(p->draw,&Outline::Move,nullptr,nullptr);hb_draw_funcs_set_line_to_func(p->draw,&Outline::Line,nullptr,nullptr);hb_draw_funcs_set_quadratic_to_func(p->draw,&Outline::Quadratic,nullptr,nullptr);hb_draw_funcs_set_cubic_to_func(p->draw,&Outline::Cubic,nullptr,nullptr);hb_draw_funcs_set_close_path_func(p->draw,&Outline::Finish,nullptr,nullptr);hb_draw_funcs_make_immutable(p->draw);impl_=std::move(p);
}
ScreenUIFontMSDFGenerator::~ScreenUIFontMSDFGenerator()=default;
bool ScreenUIFontMSDFGenerator::IsValid() const noexcept{return impl_!=nullptr;}
bool ScreenUIFontMSDFGenerator::Rasterize(std::uint32_t glyph,float scale,Bitmap& output) const {
    if(!impl_||!std::isfinite(scale)||scale<=0||glyph>=hb_face_get_glyph_count(impl_->face))return false;
    hb_glyph_extents_t extents{};
    if(hb_font_get_glyph_extents(impl_->font,glyph,&extents)&&extents.width==0&&extents.height==0){output={};return true;}
    Outline outline;if(!hb_font_draw_glyph_or_fail(impl_->font,glyph,impl_->draw,&outline)) return false;outline.Close();
    std::erase_if(outline.shape.contours,[](const auto& contour){return contour.edges.empty();});
    if(outline.shape.contours.empty()){output={};return true;}
    outline.shape.normalize();if(!outline.shape.validate())return false;outline.shape.orientContours();
    const auto bounds=outline.shape.getBounds();
    for(double value:{bounds.l,bounds.b,bounds.r,bounds.t})if(!std::isfinite(value)||std::abs(value*scale)>1000000)return false;
    Bitmap bitmap;bitmap.x0=static_cast<int>(std::floor(bounds.l*scale))-kFontDistanceFieldPadding;bitmap.x1=static_cast<int>(std::ceil(bounds.r*scale))+kFontDistanceFieldPadding;
    bitmap.y0=-static_cast<int>(std::ceil(bounds.t*scale))-kFontDistanceFieldPadding;bitmap.y1=-static_cast<int>(std::floor(bounds.b*scale))+kFontDistanceFieldPadding;
    const int width=bitmap.x1-bitmap.x0,height=bitmap.y1-bitmap.y0;if(width<=0||height<=0||width>512||height>512)return false;
    msdfgen::edgeColoringSimple(outline.shape,3.0,0);
    msdfgen::Bitmap<float,3> field(width,height,msdfgen::Y_UPWARD);
    const msdfgen::Projection projection{{scale,scale},{-static_cast<double>(bitmap.x0)/scale,static_cast<double>(bitmap.y1)/scale}};
    const msdfgen::SDFTransformation transform{projection,msdfgen::DistanceMapping{msdfgen::Range{16.0/scale}}};
    msdfgen::generateMSDF(field,outline.shape,transform);
    bitmap.rgba.resize(static_cast<std::size_t>(width)*height*4);
    for(int row=0;row<height;++row)for(int column=0;column<width;++column){const auto* pixel=field(column,height-1-row);const auto index=(static_cast<std::size_t>(row)*width+column)*4;
        for(std::size_t channel=0;channel<3;++channel){if(!std::isfinite(pixel[channel]))return false;bitmap.rgba[index+channel]=static_cast<std::uint8_t>(std::clamp(std::lround(pixel[channel]*255.0),0L,255L));}bitmap.rgba[index+3]=255;}
    output=std::move(bitmap);return true;
}
}
