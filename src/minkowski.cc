#define BOOST_POLYGON_NO_DEPS

#include <limits>
#include <napi.h>
#include "polygon/polygon.hpp"

#undef min
#undef max

typedef boost::polygon::point_data<int> point;
typedef boost::polygon::polygon_set_data<int> polygon_set;
typedef boost::polygon::polygon_with_holes_data<int> polygon;
typedef std::pair<point, point> edge;
using namespace boost::polygon::operators;

void convolve_two_segments(std::vector<point>& figure, const edge& a, const edge& b) {
  using namespace boost::polygon;
  figure.clear();
  figure.push_back(point(a.first));
  figure.push_back(point(a.first));
  figure.push_back(point(a.second));
  figure.push_back(point(a.second));
  convolve(figure[0], b.second);
  convolve(figure[1], b.first);
  convolve(figure[2], b.first);
  convolve(figure[3], b.second);
}

template <typename itrT1, typename itrT2>
void convolve_two_point_sequences(polygon_set& result, itrT1 ab, itrT1 ae, itrT2 bb, itrT2 be) {
  using namespace boost::polygon;
  if (ab == ae || bb == be)
    return;
  point first_a = *ab;
  point prev_a = *ab;
  std::vector<point> vec;
  polygon poly;
  ++ab;
  for (; ab != ae; ++ab) {
    point first_b = *bb;
    point prev_b = *bb;
    itrT2 tmpb = bb;
    ++tmpb;
    for (; tmpb != be; ++tmpb) {
      convolve_two_segments(vec, std::make_pair(prev_b, *tmpb), std::make_pair(prev_a, *ab));
      set_points(poly, vec.begin(), vec.end());
      result.insert(poly);
      prev_b = *tmpb;
    }
    prev_a = *ab;
  }
}

template <typename itrT>
void convolve_point_sequence_with_polygons(polygon_set& result, itrT b, itrT e, const std::vector<polygon>& polygons, bool& hasHoles) {
  using namespace boost::polygon;
  for (std::size_t i = 0; i < polygons.size(); ++i) {
    convolve_two_point_sequences(result, b, e, begin_points(polygons[i]), end_points(polygons[i]));
    if (hasHoles) {
      for (polygon_with_holes_traits<polygon>::iterator_holes_type itrh = begin_holes(polygons[i]);
          itrh != end_holes(polygons[i]); ++itrh) {
        convolve_two_point_sequences(result, b, e, begin_points(*itrh), end_points(*itrh));
      }
    }
  }
}

void convolve_two_polygon_sets(polygon_set& result, const polygon_set& a, const polygon_set& b, bool& hasHoles) {
  using namespace boost::polygon;
  result.clear();
  std::vector<polygon> a_polygons;
  std::vector<polygon> b_polygons;
  a.get(a_polygons);
  b.get(b_polygons);
  for (std::size_t ai = 0; ai < a_polygons.size(); ++ai) {
    convolve_point_sequence_with_polygons(result, begin_points(a_polygons[ai]), end_points(a_polygons[ai]), b_polygons, hasHoles);
    if (hasHoles) {
      for (polygon_with_holes_traits<polygon>::iterator_holes_type itrh = begin_holes(a_polygons[ai]);
          itrh != end_holes(a_polygons[ai]); ++itrh) {
        convolve_point_sequence_with_polygons(result, begin_points(*itrh), end_points(*itrh), b_polygons, hasHoles);
      }
    }
    for (std::size_t bi = 0; bi < b_polygons.size(); ++bi) {
      polygon tmp_poly = a_polygons[ai];
      result.insert(convolve(tmp_poly, *(begin_points(b_polygons[bi]))));
      tmp_poly = b_polygons[bi];
      result.insert(convolve(tmp_poly, *(begin_points(a_polygons[ai]))));
    }
  }
}

double calculateInputScale(const Napi::Array& A, const Napi::Array& B) {
    unsigned int len = A.Length();
    double Amaxx = 0, Aminx = 0, Amaxy = 0, Aminy = 0;
    for (unsigned int i = 0; i < len; i++) {
        Napi::Object obj = A.Get(i).As<Napi::Object>();
        double x = obj.Get("X").ToNumber().DoubleValue();
        double y = obj.Get("Y").ToNumber().DoubleValue();

        Amaxx = (std::max)(Amaxx, x);
        Aminx = (std::min)(Aminx, x);
        Amaxy = (std::max)(Amaxy, y);
        Aminy = (std::min)(Aminy, y);
    }

    len = B.Length();
    double Bmaxx = 0, Bminx = 0, Bmaxy = 0, Bminy = 0;
    for (unsigned int i = 0; i < len; i++) {
        Napi::Object obj = B.Get(i).As<Napi::Object>();
        double x = obj.Get("X").ToNumber().DoubleValue();
        double y = obj.Get("Y").ToNumber().DoubleValue();

        Bmaxx = (std::max)(Bmaxx, x);
        Bminx = (std::min)(Bminx, x);
        Bmaxy = (std::max)(Bmaxy, y);
        Bminy = (std::min)(Bminy, y);
    }

    double Cmaxx = Amaxx + Bmaxx;
    double Cminx = Aminx + Bminx;
    double Cmaxy = Amaxy + Bmaxy;
    double Cminy = Aminy + Bminy;

    double maxxAbs = (std::max)(Cmaxx, std::fabs(Cminx));
    double maxyAbs = (std::max)(Cmaxy, std::fabs(Cminy));
    double maxda = (std::max)(maxxAbs, maxyAbs);
    if (maxda < 1) {
        maxda = 1;
    }
    return ((0.1f * std::numeric_limits<int>::max()) / maxda);
}

Napi::Value calculateNFP(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object group = info[0].As<Napi::Object>();
    Napi::Object A = group.Get("A").As<Napi::Object>();
    Napi::Object B = group.Get("B").As<Napi::Object>();
    Napi::Array aPoints = A.Get("points").As<Napi::Array>();
    Napi::Array bPoints = B.Get("points").As<Napi::Array>();
    bool hasHoles = group.Has("hasHoles") && group.Get("hasHoles").ToBoolean().Value();
    Napi::Array holes = hasHoles && A.Has("children") ? A.Get("children").As<Napi::Array>() : Napi::Array::New(env);

    polygon_set a, b, c;
    std::vector<polygon> polys;
    std::vector<point> pts;
    double inputscale = calculateInputScale(aPoints, bPoints);

    // Carica i punti di A
    for (unsigned int i = 0; i < aPoints.Length(); i++) {
        Napi::Object obj = aPoints.Get(i).As<Napi::Object>();
        int x = static_cast<int>(inputscale * obj.Get("X").ToNumber().DoubleValue());
        int y = static_cast<int>(inputscale * obj.Get("Y").ToNumber().DoubleValue());
        pts.push_back(point(x, y));
    }
    polygon poly;
    boost::polygon::set_points(poly, pts.begin(), pts.end());
    a += poly;

    // Carica i buchi di A (se presenti)
    if (hasHoles) {
        for (unsigned int i = 0; i < holes.Length(); i++) {
            Napi::Array hole = holes.Get(i).As<Napi::Array>();
            pts.clear();
            for (unsigned int j = 0; j < hole.Length(); j++) {
                Napi::Object obj = hole.Get(j).As<Napi::Object>();
                int x = static_cast<int>(inputscale * obj.Get("X").ToNumber().DoubleValue());
                int y = static_cast<int>(inputscale * obj.Get("Y").ToNumber().DoubleValue());
                pts.push_back(point(x, y));
            }
            boost::polygon::set_points(poly, pts.begin(), pts.end());
            a -= poly; // Sottrae il buco dal poligono principale
        }
    }

    // Carica i punti di B
    pts.clear();
    double xshift = 0, yshift = 0;
    for (unsigned int i = 0; i < bPoints.Length(); i++) {
        Napi::Object obj = bPoints.Get(i).As<Napi::Object>();
        int x = -static_cast<int>(inputscale * obj.Get("X").ToNumber().DoubleValue());
        int y = -static_cast<int>(inputscale * obj.Get("Y").ToNumber().DoubleValue());
        pts.push_back(point(x, y));
        if (i == 0) {
            xshift = obj.Get("X").ToNumber().DoubleValue();
            yshift = obj.Get("Y").ToNumber().DoubleValue();
        }
    }
    boost::polygon::set_points(poly, pts.begin(), pts.end());
    b += poly;

    // Calcola la convoluzione
    polys.clear();
    convolve_two_polygon_sets(c, a, b, hasHoles);
    c.get(polys);

    // Costruisci il risultato
    Napi::Array result_list = Napi::Array::New(env, polys.size());
    for (unsigned int i = 0; i < polys.size(); ++i) {
        Napi::Array pointlist = Napi::Array::New(env);
        int j = 0;
        for (auto itr = polys[i].begin(); itr != polys[i].end(); ++itr) {
            Napi::Object p = Napi::Object::New(env);
            p.Set("X", Napi::Number::New(env, ((double)(*itr).x()) / inputscale + xshift));
            p.Set("Y", Napi::Number::New(env, ((double)(*itr).y()) / inputscale + yshift));
            pointlist.Set(j, p);
            j++;
        }

        // Aggiungi i buchi (se presenti)
        if (hasHoles) {
            Napi::Array children = Napi::Array::New(env);
            int k = 0;
            for (auto itrh = polys[i].begin_holes(); itrh != polys[i].end_holes(); ++itrh) {
                Napi::Array child = Napi::Array::New(env);
                int z = 0;
                for (auto itr2 = (*itrh).begin(); itr2 != (*itrh).end(); ++itr2) {
                    Napi::Object localC = Napi::Object::New(env);
                    localC.Set("X", Napi::Number::New(env, ((double)(*itr2).x()) / inputscale + xshift));
                    localC.Set("Y", Napi::Number::New(env, ((double)(*itr2).y()) / inputscale + yshift));
                    child.Set(z, localC);
                    z++;
                }
                children.Set(k, child);
                k++;
            }
            pointlist.Set("children", children);
        }
        result_list.Set(i, pointlist);
    }
    return result_list;
}