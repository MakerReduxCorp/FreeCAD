/***************************************************************************
 *   Copyright (c) Yorik van Havre (yorik@uncreated.net) 2014              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
# include <Inventor/SbVec3f.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoTransform.h>
# include <Inventor/nodes/SoRotation.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoMaterialBinding.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoMarkerSet.h>
# include <Inventor/nodes/SoShapeHints.h>
# include <QFile>
#endif

#include "ViewProviderPath.h"

#include <Mod/Path/App/FeaturePath.h>
#include <Mod/Path/App/Path.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Gui/BitmapFactory.h>
#include <Gui/SoFCBoundingBox.h>


#define ARC_MIN_SEGMENTS   20.0  // minimum # segements to interpolate an arc

#ifndef M_PI
    #define M_PI 3.14159265358979323846
    #define M_PI    3.14159265358979323846 /* pi */
#endif

#ifndef M_PI_2
    #define M_PI_2  1.57079632679489661923 /* pi/2 */
#endif

using namespace Gui;
using namespace PathGui;
using namespace Path;
using namespace PartGui;

PROPERTY_SOURCE(PathGui::ViewProviderPath, Gui::ViewProviderGeometryObject)

ViewProviderPath::ViewProviderPath()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Path");
    unsigned long lcol = hGrp->GetUnsigned("DefaultNormalPathColor",11141375UL); // dark green (0,170,0)
    float lr,lg,lb;
    lr = ((lcol >> 24) & 0xff) / 255.0; lg = ((lcol >> 16) & 0xff) / 255.0; lb = ((lcol >> 8) & 0xff) / 255.0;
    unsigned long mcol = hGrp->GetUnsigned("DefaultPathMarkerColor",1442775295UL); // lime green (85,255,0)
    float mr,mg,mb;
    mr = ((mcol >> 24) & 0xff) / 255.0; mg = ((mcol >> 16) & 0xff) / 255.0; mb = ((mcol >> 8) & 0xff) / 255.0;
    int lwidth = hGrp->GetInt("DefaultPathLineWidth",1);
    ADD_PROPERTY_TYPE(NormalColor,(lr,lg,lb),"Path",App::Prop_None,"The color of the feed rate moves");
    ADD_PROPERTY_TYPE(MarkerColor,(mr,mg,mb),"Path",App::Prop_None,"The color of the markers");
    ADD_PROPERTY_TYPE(LineWidth,(lwidth),"Path",App::Prop_None,"The line width of this path");
    ADD_PROPERTY_TYPE(ShowFirstRapid,(true),"Path",App::Prop_None,"Turns the display of the first rapid move on/off");
    ADD_PROPERTY_TYPE(ShowNodes,(false),"Path",App::Prop_None,"Turns the display of nodes on/off");

    pcPathRoot = new Gui::SoFCSelection();

    pcPathRoot->style = Gui::SoFCSelection::EMISSIVE;
    pcPathRoot->highlightMode = Gui::SoFCSelection::AUTO;
    pcPathRoot->selectionMode = Gui::SoFCSelection::SEL_ON;
    pcPathRoot->ref();

    pcTransform = new SoTransform();
    pcTransform->ref();

    pcLineCoords = new SoCoordinate3();
    pcLineCoords->ref();

    pcMarkerCoords = new SoCoordinate3();
    pcMarkerCoords->ref();

    pcDrawStyle = new SoDrawStyle();
    pcDrawStyle->ref();
    pcDrawStyle->style = SoDrawStyle::LINES;
    pcDrawStyle->lineWidth = LineWidth.getValue();

    pcLines = new PartGui::SoBrepEdgeSet();
    pcLines->ref();
    pcLines->coordIndex.setNum(0);

    pcLineColor = new SoMaterial;
    pcLineColor->ref();

    pcMatBind = new SoMaterialBinding;
    pcMatBind->ref();
    pcMatBind->value = SoMaterialBinding::OVERALL;

    pcMarkerColor = new SoBaseColor;
    pcMarkerColor->ref();

    NormalColor.touch();
    MarkerColor.touch();
}

ViewProviderPath::~ViewProviderPath()
{
    pcPathRoot->unref();
    pcTransform->unref();
    pcLineCoords->unref();
    pcMarkerCoords->unref();
    pcDrawStyle->unref();
    pcLines->unref();
    pcLineColor->unref();
    pcMatBind->unref();
    pcMarkerColor->unref();
}

void ViewProviderPath::attach(App::DocumentObject *pcObj)
{
    ViewProviderDocumentObject::attach(pcObj);

    // Draw trajectory lines
    SoSeparator* linesep = new SoSeparator;
    linesep->addChild(pcLineColor);
    linesep->addChild(pcMatBind);
    linesep->addChild(pcDrawStyle);
    linesep->addChild(pcLineCoords);
    linesep->addChild(pcLines);

    // Draw markers
    SoSeparator* markersep = new SoSeparator;
    SoMarkerSet* marker = new SoMarkerSet;
    marker->markerIndex=SoMarkerSet::PLUS_7_7;
    markersep->addChild(pcMarkerColor);
    markersep->addChild(pcMarkerCoords);
    markersep->addChild(marker);

    pcPathRoot->addChild(pcTransform);
    pcPathRoot->addChild(linesep);
    pcPathRoot->addChild(markersep);

    addDisplayMaskMode(pcPathRoot, "Waypoints");
    pcPathRoot->objectName = pcObj->getNameInDocument();
    pcPathRoot->documentName = pcObj->getDocument()->getName();
    pcPathRoot->subElementName = "Path";
}

void ViewProviderPath::setDisplayMode(const char* ModeName)
{
    if ( strcmp("Waypoints",ModeName)==0 )
        setDisplayMaskMode("Waypoints");
    ViewProviderGeometryObject::setDisplayMode( ModeName );
}

std::vector<std::string> ViewProviderPath::getDisplayModes(void) const
{
    std::vector<std::string> StrList;
    StrList.push_back("Waypoints");
    return StrList;
}

void ViewProviderPath::onChanged(const App::Property* prop)
{
    if (prop == &LineWidth) {
        pcDrawStyle->lineWidth = LineWidth.getValue();
    } else if (prop == &NormalColor) {
        if (colorindex.size() > 0) {
            const App::Color& c = NormalColor.getValue();
            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Path");
            unsigned long rcol = hGrp->GetUnsigned("DefaultRapidPathColor",2852126975UL); // dark red (170,0,0)
            float rr,rg,rb;
            rr = ((rcol >> 24) & 0xff) / 255.0; rg = ((rcol >> 16) & 0xff) / 255.0; rb = ((rcol >> 8) & 0xff) / 255.0;

            unsigned long pcol = hGrp->GetUnsigned("DefaultProbePathColor",4293591295UL); // yellow (255,255,5)
            float pr,pg,pb;
            pr = ((pcol >> 24) & 0xff) / 255.0; pg = ((pcol >> 16) & 0xff) / 255.0; pb = ((pcol >> 8) & 0xff) / 255.0;

            pcMatBind->value = SoMaterialBinding::PER_PART;
            // resizing and writing the color vector:
            pcLineColor->diffuseColor.setNum(colorindex.size());
            SbColor* colors = pcLineColor->diffuseColor.startEditing();
            for(unsigned int i=0;i<colorindex.size();i++) {
                if (colorindex[i] == 0)
                    colors[i] = SbColor(rr,rg,rb);
                else if (colorindex[i] == 1)
                    colors[i] = SbColor(c.r,c.g,c.b);
                else
                    colors[i] = SbColor(pr,pg,pb);
            }
            pcLineColor->diffuseColor.finishEditing();
        }
    } else if (prop == &MarkerColor) {
        const App::Color& c = MarkerColor.getValue();
        pcMarkerColor->rgb.setValue(c.r,c.g,c.b);
    } else if ( (prop == &ShowFirstRapid) || (prop == &ShowNodes) ) {
        Path::Feature* pcPathObj = static_cast<Path::Feature*>(pcObject);
        this->updateData(&pcPathObj->Path);
    } else {
        ViewProviderGeometryObject::onChanged(prop);
    }
}

void ViewProviderPath::updateData(const App::Property* prop)
{
    Path::Feature* pcPathObj = static_cast<Path::Feature*>(pcObject);

    if (prop == &pcPathObj->Path) {

        pcLineCoords->point.deleteValues(0);
        pcMarkerCoords->point.deleteValues(0);

        const Toolpath &tp = pcPathObj->Path.getValue();
        if(tp.getSize()==0) {
            return;
        }

        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part");
        float deviation = hGrp->GetFloat("MeshDeviation",0.2);
        std::vector<Base::Vector3d> points;
        std::vector<Base::Vector3d> markers;
        Base::Vector3d last(0,0,0);
        colorindex.clear();
        bool absolute = true;
        bool absolutecenter = false;
        bool first = true;

        // for mapping the coordinates to XY plane
        double Base::Vector3d::*pz = &Base::Vector3d::z;

        for (unsigned int  i = 0; i < tp.getSize(); i++) {
            Path::Command cmd = tp.getCommand(i);
            std::string name = cmd.Name;
            Base::Vector3d next = cmd.getPlacement().getPosition();
            if (!absolute)
                next = last + next;
            if (!cmd.has("X"))
                next.x = last.x;
            if (!cmd.has("Y"))
                next.y = last.y;
            if (!cmd.has("Z"))
                next.z = last.z;

            if ( (name == "G0") || (name == "G00") || (name == "G1") || (name == "G01") ) {
                // straight line
                if ( (!first) || (ShowFirstRapid.getValue() == true) || (name == "G1") || (name == "G01") ) {
                    if (first) {
                        points.push_back(last);
                        markers.push_back(last); // startpoint of path
                    }
                    points.push_back(next);
                    if (ShowNodes.getValue() == true)
                        markers.push_back(next); // endpoint
                    last = next;
                    if ( (name == "G0") || (name == "G00") )
                        colorindex.push_back(0); // rapid color
                    else
                        colorindex.push_back(1); // std color
                } else {
                    // don't show first G0 move if ShowFirstRapid is False
                    last = next;
                    points.push_back(last);
                    markers.push_back(last); // startpoint of path
                }
                first = false;

            } else if ( (name == "G2") || (name == "G02") || (name == "G3") || (name == "G03") ) {
                // arc
                Base::Vector3d norm;
                Base::Vector3d center;

                if ( (name == "G2") || (name == "G02") )
                    norm.*pz = -1.0;
                else
                    norm.*pz = 1.0;

                if (absolutecenter)
                    center = cmd.getCenter();
                else
                    center = (last + cmd.getCenter());
                Base::Vector3d next0(next);
                next0.*pz = 0.0;
                Base::Vector3d last0(last);
                last0.*pz = 0.0;
                Base::Vector3d center0(center);
                center0.*pz = 0.0;
                //double radius = (last - center).Length();
                double angle = (next0 - center0).GetAngle(last0 - center0);
                // GetAngle will always return the minor angle. Switch if needed
                Base::Vector3d anorm = (last0 - center0) % (next0 - center0);
                if(anorm.*pz < 0) {
                    if(name == "G3" || name == "G03")
                        angle = M_PI * 2 - angle;
                }else if(anorm.*pz > 0) {
                    if(name == "G2" || name == "G02")
                        angle = M_PI * 2 - angle;
                }else if (angle == 0)
                    angle = M_PI * 2;
                int segments = std::max(ARC_MIN_SEGMENTS, 3.0/(deviation/angle)); //we use a rather simple rule here, provisorily
                double dZ = (next.*pz - last.*pz)/segments; //How far each segment will helix in Z

                for (int j = 1; j < segments; j++) {
                    Base::Vector3d inter;
                    Base::Rotation rot(norm,(angle/segments)*j);
                    rot.multVec((last0 - center0),inter);
                    inter.*pz = last.*pz + dZ * j; //Enable displaying helices
                    points.push_back( center0 + inter);
                    colorindex.push_back(1);
                }
                points.push_back(next);
                if (ShowNodes.getValue() == true) {
                    markers.push_back(next); // endpoint
                    markers.push_back(center); // add a marker at center too
                }
                last = next;
                colorindex.push_back(1);

            } else if (name == "G90") {
                // absolute mode
                absolute = true;

            } else if (name == "G91") {
                // relative mode
                absolute = false;

            } else if (name == "G90.1") {
                // absolute mode
                absolutecenter = true;

            } else if (name == "G91.1") {
                // relative mode
                absolutecenter = false;

            } else if ((name=="G81")||(name=="G82")||(name=="G83")||(name=="G84")||(name=="G85")||(name=="G86")||(name=="G89")){
                // drill,tap,bore
                double r = 0;
                if (cmd.has("R"))
                    r = cmd.getValue("R");
                Base::Vector3d p1(next);
                p1.*pz = last.*pz;
                points.push_back(p1);
                if (ShowNodes.getValue() == true)
                    markers.push_back(p1);
                colorindex.push_back(0);
                Base::Vector3d p2(next);
                p2.*pz = r;
                points.push_back(p2);
                if (ShowNodes.getValue() == true)
                    markers.push_back(p2);
                colorindex.push_back(0);
                points.push_back(next);
                if (ShowNodes.getValue() == true)
                    markers.push_back(next);
                colorindex.push_back(1);
                double q;
                if (cmd.has("Q")) {
                    q = cmd.getValue("Q");
                    if (q>0) {
                        Base::Vector3d temp(next);
                        for(temp.*pz=r;temp.*pz>next.*pz;temp.*pz-=q)
                            markers.push_back(temp);
                    }
                }
                Base::Vector3d p3(next);
                p3.*pz = last.*pz;
                points.push_back(p3);
                if (ShowNodes.getValue() == true)
                    markers.push_back(p2);
                colorindex.push_back(0);

            } else if ((name=="G38.2")||(name=="38.3")||(name=="G38.4")||(name=="G38.5")){
                // Straight probe
                Base::Vector3d p1(next.x,next.y,last.z);
                points.push_back(p1);
                colorindex.push_back(0);
                points.push_back(next);
                colorindex.push_back(2);
                Base::Vector3d p3(next.x,next.y,last.z);
                points.push_back(p3);
                colorindex.push_back(0);
            } else if(name=="G17") {
                pz = &Base::Vector3d::z;
            } else if(name=="G18") {
                pz = &Base::Vector3d::y;
            } else if(name=="G19") {
                pz = &Base::Vector3d::x;
            }}

        if (!points.empty()) {
            pcLineCoords->point.deleteValues(0);
            pcLineCoords->point.setNum(points.size());
            std::vector<int> ei;
            for(unsigned int i=0;i<points.size();i++) {
                pcLineCoords->point.set1Value(i,points[i].x,points[i].y,points[i].z);
                ei.push_back(i);
            }
            int* segs = &ei[0];
            pcLines->coordIndex.setNum(points.size());
            pcLines->coordIndex.setValues(0,points.size(),(const int32_t*)segs);

            pcMarkerCoords->point.deleteValues(0);

            pcMarkerCoords->point.setNum(markers.size());
            for(unsigned int i=0;i<markers.size();i++)
                pcMarkerCoords->point.set1Value(i,markers[i].x,markers[i].y,markers[i].z);

            // update the coloring after we changed the color vector
            NormalColor.touch();
            recomputeBoundingBox();
        }

    } else if ( prop == &pcPathObj->Placement) {

        Base::Placement pl = *(&pcPathObj->Placement.getValue());
        Base::Vector3d pos = pl.getPosition();
        double q1, q2, q3, q4;
        pl.getRotation().getValue(q1,q2,q3,q4);
        pcTransform->translation.setValue(pos.x,pos.y,pos.z);
        pcTransform->rotation.setValue(q1,q2,q3,q4);
        recomputeBoundingBox();
    }
}

void ViewProviderPath::recomputeBoundingBox()
{
    // update the boundbox
    double MinX,MinY,MinZ,MaxX,MaxY,MaxZ;
    MinX = 999999999.0;
    MinY = 999999999.0;
    MinZ = 999999999.0;
    MaxX = -999999999.0;
    MaxY = -999999999.0;
    MaxZ = -999999999.0;
    Path::Feature* pcPathObj = static_cast<Path::Feature*>(pcObject);
    Base::Placement pl = *(&pcPathObj->Placement.getValue());
    Base::Vector3d pt;
    for (int i=0;i<pcLineCoords->point.getNum();i++) {
        pt.x = pcLineCoords->point[i].getValue()[0];
        pt.y = pcLineCoords->point[i].getValue()[1];
        pt.z = pcLineCoords->point[i].getValue()[2];
        pl.multVec(pt,pt);
        if (pt.x < MinX)  MinX = pt.x;
        if (pt.y < MinY)  MinY = pt.y;
        if (pt.z < MinZ)  MinZ = pt.z;
        if (pt.x > MaxX)  MaxX = pt.x;
        if (pt.y > MaxY)  MaxY = pt.y;
        if (pt.z > MaxZ)  MaxZ = pt.z;
    }
    pcBoundingBox->minBounds.setValue(MinX, MinY, MinZ);
    pcBoundingBox->maxBounds.setValue(MaxX, MaxY, MaxZ);
}

QIcon ViewProviderPath::getIcon() const
{
    return Gui::BitmapFactory().pixmap("Path-Toolpath");
}

// Python object -----------------------------------------------------------------------

namespace Gui {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PathGui::ViewProviderPathPython, PathGui::ViewProviderPath)
/// @endcond

// explicit template instantiation
template class PathGuiExport ViewProviderPythonFeatureT<PathGui::ViewProviderPath>;
}

