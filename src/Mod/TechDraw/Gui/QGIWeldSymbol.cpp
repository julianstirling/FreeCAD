/***************************************************************************
 *   Copyright (c) 2019 WandererFan <wandererfan@gmail.com>                *
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
  #include <BRep_Builder.hxx>
  #include <TopoDS_Compound.hxx>
  # include <TopoDS_Shape.hxx>
  # include <TopoDS_Edge.hxx>
  # include <TopoDS.hxx>
  # include <BRepAdaptor_Curve.hxx>
  # include <Precision.hxx>

  # include <QGraphicsScene>
  # include <QPainter>
  # include <QPaintDevice>
  # include <QSvgGenerator>

  # include <math.h>
#endif

#include <App/Application.h>
#include <App/Material.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/UnitsApi.h>
#include <Gui/Command.h>

#include <Mod/Part/App/PartFeature.h>

#include <Mod/TechDraw/App/DrawWeldSymbol.h>
#include <Mod/TechDraw/App/DrawLeaderLine.h>
#include <Mod/TechDraw/App/DrawTile.h>
#include <Mod/TechDraw/App/DrawTileWeld.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/Geometry.h>

#include "Rez.h"
#include "ZVALUE.h"
#include "ViewProviderWeld.h"
#include "MDIViewPage.h"
#include "DrawGuiUtil.h"
#include "QGVPage.h"
#include "QGIPrimPath.h"
#include "QGITile.h"
#include "QGILeaderLine.h"
#include "QGIVertex.h"
#include "QGCustomText.h"

#include "QGIWeldSymbol.h"

using namespace TechDraw;
using namespace TechDrawGui;


//**************************************************************
QGIWeldSymbol::QGIWeldSymbol(QGILeaderLine* myParent) :
    m_weldFeat(nullptr),
    m_leadFeat(nullptr),
    m_arrowFeat(nullptr),
    m_otherFeat(nullptr),
    m_qgLead(myParent),
    m_blockDraw(false)
{
#if PY_MAJOR_VERSION < 3
    setHandlesChildEvents(true);    //qt4 deprecated in qt5
#else
    setFiltersChildEvents(true);    //qt5
#endif
    setFlag(QGraphicsItem::ItemIsMovable, false);
    
    setCacheMode(QGraphicsItem::NoCache);

    setParentItem(m_qgLead);
    m_leadFeat = m_qgLead->getFeature();
    setZValue(ZVALUE::DIMENSION);

    m_tailText = new QGCustomText();
    m_tailText->setPlainText(
                QString::fromUtf8(" "));
    addToGroup(m_tailText);
    m_tailText->hide();
    m_tailText->setPos(0.0, 0.0);         //avoid bRect issues

    m_allAround = new QGIVertex(-1);
    addToGroup(m_allAround);
    m_allAround->setPos(0.0, 0.0);
    m_allAround->setAcceptHoverEvents(false);
    m_allAround->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_allAround->setFlag(QGraphicsItem::ItemIsMovable, false);
    m_allAround->setFlag(QGraphicsItem::ItemSendsScenePositionChanges, false);
    m_allAround->setFlag(QGraphicsItem::ItemSendsGeometryChanges,true);
    m_allAround->setFlag(QGraphicsItem::ItemStacksBehindParent, true);

    m_fieldFlag = new QGIPrimPath();
    addToGroup(m_fieldFlag);
    m_fieldFlag->setPos(0.0, 0.0);
    m_fieldFlag->setAcceptHoverEvents(false);
    m_fieldFlag->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_fieldFlag->setFlag(QGraphicsItem::ItemIsMovable, false);
    m_fieldFlag->setFlag(QGraphicsItem::ItemSendsScenePositionChanges, false);
    m_fieldFlag->setFlag(QGraphicsItem::ItemSendsGeometryChanges,true);
    m_fieldFlag->setFlag(QGraphicsItem::ItemStacksBehindParent, true);

    m_colCurrent = prefNormalColor();
    m_colSetting = m_colCurrent;
}

QVariant QGIWeldSymbol::itemChange(GraphicsItemChange change, const QVariant &value)
{
//    Base::Console().Message("QGIWS::itemChange(%d)\n", change);
    if (change == ItemSelectedHasChanged && scene()) {
        if(isSelected()) {
            setPrettySel();
        } else {
            setPrettyNormal();
        }
    } else if(change == ItemSceneChange && scene()) {
        // nothing special!
    }
    return QGIView::itemChange(change, value);
}

void QGIWeldSymbol::updateView(bool update)
{
//    Base::Console().Message("QGIWS::updateView()\n");
    Q_UNUSED(update);
    auto viewWeld( dynamic_cast<TechDraw::DrawWeldSymbol*>(getViewObject()) );
    if( viewWeld == nullptr ) {
        return;
    }

    if ( getFeature() == nullptr ) {
        Base::Console().Warning("QGIWS::updateView - no feature!\n");
        return;
    }

    draw();
}

void QGIWeldSymbol::draw()
{
//    Base::Console().Message("QGIWS::draw()- %s\n", getFeature()->getNameInDocument());

    if (!isVisible()) {
        return;
    }
    getTileFeats();

    removeQGITiles();

    if (m_arrowFeat != nullptr) {
        drawTile(m_arrowFeat);
    }

    if (m_otherFeat != nullptr) {
        drawTile(m_otherFeat);
    }

    drawAllAround();

    drawFieldFlag();

    drawTailText();
}

void QGIWeldSymbol::drawTile(TechDraw::DrawTileWeld* tileFeat)
{
//    Base::Console().Message("QGIWS::drawTile() - tileFeat: %X\n", tileFeat);
    if (tileFeat == nullptr) {
        Base::Console().Message("QGIWS::drawTile - tile is null\n");
        return;
    }

    double featScale = m_leadFeat->getScale();

    std::string tileTextL = tileFeat->LeftText.getValue();
    std::string tileTextR = tileFeat->RightText.getValue();
    std::string tileTextC = tileFeat->CenterText.getValue();
    std::string symbolFile = tileFeat->SymbolFile.getValue();
    int row = tileFeat->TileRow.getValue();
    int col = tileFeat->TileColumn.getValue();

    QGITile* tile = new QGITile();
    addToGroup(tile);

    QPointF org = getTileOrigin();
    tile->setTilePosition(org, row, col);
    tile->setColor(getCurrentColor());
    tile->setTileTextLeft(tileTextL);
    tile->setTileTextRight(tileTextR);
    tile->setTileTextCenter(tileTextC);
    tile->setSymbolFile(symbolFile);
    tile->setZValue(ZVALUE::DIMENSION);
    tile->setTileScale(featScale);
    tile->setTailRight(m_weldFeat->isTailRightSide());
    tile->setAltWeld(m_weldFeat->AlternatingWeld.getValue());

    tile->draw();
}

void QGIWeldSymbol::drawAllAround(void)
{
//    Base::Console().Message("QGIWS::drawAllAround()\n");
    if (getFeature()->AllAround.getValue()) {
        m_allAround->show();
    } else {
        m_allAround->hide();
        return;
    }

    m_allAround->setNormalColor(getCurrentColor());

    m_allAround->setFill(Qt::NoBrush);
    m_allAround->setRadius(calculateFontPixelSize(getDimFontSize()));
    double width = m_qgLead->getLineWidth();
    m_allAround->setWidth(width);
    m_allAround->setZValue(ZVALUE::DIMENSION);

    QPointF allAroundPos = getKinkPoint();
    m_allAround->setPos(allAroundPos);
}

void QGIWeldSymbol::drawTailText(void)
{
//    Base::Console().Message("QGIWS::drawTailText()\n");
    QPointF textPos = getTailPoint();
    m_tailText->setPos(textPos);  //avoid messing up brect with empty item at 0,0
    std::string tText = getFeature()->TailText.getValue();
    if (tText.empty()) {
        m_tailText->hide();
        return;
    } else {
        m_tailText->show();
    }

    m_font.setFamily(getPrefFont());
    m_font.setPixelSize(calculateFontPixelSize(getDimFontSize()));

    m_tailText->setFont(m_font);
    m_tailText->setPlainText(
                QString::fromUtf8(tText.c_str()));
    m_tailText->setColor(getCurrentColor());
    m_tailText->setZValue(ZVALUE::DIMENSION);

    double textWidth = m_tailText->boundingRect().width();
    double charWidth = textWidth / tText.size();
    double hMargin = charWidth + prefArrowSize();

    if (getFeature()->isTailRightSide()) {
        m_tailText->justifyLeftAt(textPos.x() + hMargin, textPos.y(), true);
    } else {
        m_tailText->justifyRightAt(textPos.x() - hMargin, textPos.y(), true);
    }
}

void QGIWeldSymbol::drawFieldFlag()
{
//    Base::Console().Message("QGIWS::drawFieldFlag()\n");
    if (getFeature()->FieldWeld.getValue()) {
        m_fieldFlag->show();
    } else {
        m_fieldFlag->hide();
        return;
    }
    std::vector<QPointF> flagPoints = { QPointF(0.0, 0.0),
                                        QPointF(0.0, -3.0),
                                        QPointF(-2.0, -2.5),
                                        QPointF(0.0, -2.0) };
    //flag sb about 2x text?
    double scale = calculateFontPixelSize(getDimFontSize()) / 2.0;
    QPainterPath path;
    path.moveTo(flagPoints.at(0) * scale);
    int i = 1;
    int stop = flagPoints.size();
    for ( ; i < stop; i++) {
        path.lineTo(flagPoints.at(i) * scale);
    }

    m_fieldFlag->setNormalColor(getCurrentColor());     //penColor
    double width = m_qgLead->getLineWidth();

    m_fieldFlag->setFillColor(getCurrentColor());
    m_fieldFlag->setFill(Qt::SolidPattern);

    m_fieldFlag->setWidth(width);
    m_fieldFlag->setZValue(ZVALUE::DIMENSION);

    m_fieldFlag->setPath(path);

    QPointF fieldFlagPos = getKinkPoint();
    m_fieldFlag->setPos(fieldFlagPos); 
}

void QGIWeldSymbol::getTileFeats(void)
{
    std::vector<TechDraw::DrawTileWeld*> tiles = getFeature()->getTiles();
    m_arrowFeat = nullptr;
    m_otherFeat = nullptr;
    
    if (!tiles.empty()) {
        TechDraw::DrawTileWeld* tempTile = tiles.at(0);
        if (tempTile->TileRow.getValue() == 0) {
            m_arrowFeat = tempTile;
        } else { 
            m_otherFeat = tempTile;
        }
    }
    if (tiles.size() > 1) {
        TechDraw::DrawTileWeld* tempTile = tiles.at(1);
        if (tempTile->TileRow.getValue() == 0) {
            m_arrowFeat = tempTile;
        } else { 
            m_otherFeat = tempTile;
        }
    }
}

void QGIWeldSymbol::removeQGITiles(void) 
{
    std::vector<QGITile*> tiles = getQGITiles();
    for (auto t: tiles) {
            QList<QGraphicsItem*> tChildren = t->childItems();
            for (auto tc: tChildren) {
                t->removeFromGroup(tc);
                scene()->removeItem(tc);
                //tc gets deleted when QGIWS gets deleted
            }
        removeFromGroup(t);
        scene()->removeItem(t);
        delete t;
    }
}

std::vector<QGITile*> QGIWeldSymbol::getQGITiles(void) 
{
    std::vector<QGITile*> result;
    QList<QGraphicsItem*> children = childItems();
    for (auto& c:children) {
         QGITile* tile = dynamic_cast<QGITile*>(c);
         if (tile) {
            result.push_back(tile);
         }
     }
     return result;
}

void QGIWeldSymbol::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);
    if (isSelected()) {
        m_colCurrent = getSelectColor();
        setPrettySel();
    } else {
        m_colCurrent = getPreColor();
        setPrettyPre();
    }
    QGIView::hoverEnterEvent(event);
}

void QGIWeldSymbol::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);
    if(isSelected()) {
        m_colCurrent = getSelectColor();
        setPrettySel();
    } else {
        m_colCurrent = m_colNormal;
        setPrettyNormal();
    }
    QGIView::hoverLeaveEvent(event);
}

void QGIWeldSymbol::drawBorder()
{
////Weld Symbols have no border!
//    QGIView::drawBorder();   //good for debugging
}

void QGIWeldSymbol::setPrettyNormal()
{
    std::vector<QGITile*> tiles = getQGITiles();
    for (auto t: tiles) {
        t->setColor(m_colNormal);
        t->draw();
    }
    m_colCurrent = m_colNormal;
    m_fieldFlag->setNormalColor(m_colCurrent);
    m_fieldFlag->setFillColor(m_colCurrent);
    m_fieldFlag->setPrettyNormal();
    m_allAround->setNormalColor(m_colCurrent);
    m_allAround->setPrettyNormal();
    m_tailText->setColor(m_colCurrent);
    m_tailText->setPrettyNormal();
}

void QGIWeldSymbol::setPrettyPre()
{
    std::vector<QGITile*> tiles = getQGITiles();
    for (auto t: tiles) {
        t->setColor(getPreColor());
        t->draw();
    }

    m_colCurrent = getPreColor();
    m_fieldFlag->setNormalColor(getPreColor());
    m_fieldFlag->setFillColor(getPreColor());
    m_fieldFlag->setPrettyPre();
    m_allAround->setNormalColor(getPreColor());
    m_allAround->setPrettyPre();
    m_tailText->setColor(getPreColor());
    m_tailText->setPrettyPre();
}

void QGIWeldSymbol::setPrettySel()
{
    std::vector<QGITile*> tiles = getQGITiles();
    for (auto t: tiles) {
        t->setColor(getSelectColor());
        t->draw();
    }

    m_colCurrent = getSelectColor();
    m_fieldFlag->setNormalColor(getSelectColor());
    m_fieldFlag->setFillColor(getSelectColor());
    m_fieldFlag->setPrettySel();
    m_allAround->setNormalColor(getSelectColor());
    m_allAround->setPrettySel();
    m_tailText->setColor(getSelectColor());
    m_tailText->setPrettySel();
}

QPointF QGIWeldSymbol::getTileOrigin(void)
{
    Base::Vector3d org = m_leadFeat->getTileOrigin();
    QPointF result(org.x, org.y);
    return result;
}

QPointF QGIWeldSymbol::getKinkPoint(void)
{
    Base::Vector3d org = m_leadFeat->getKinkPoint();
    QPointF result(org.x, org.y);
    return result;
}

QPointF QGIWeldSymbol::getTailPoint(void)
{
    Base::Vector3d org = m_leadFeat->getTailPoint();
    QPointF result(org.x, org.y);
    return result;
}

void QGIWeldSymbol::setFeature(TechDraw::DrawWeldSymbol* feat)
{
//    Base::Console().Message("QGIWS::setFeature(%s)\n", feat->getNameInDocument());
    m_weldFeat = feat;
    m_weldFeatName = feat->getNameInDocument();
}

TechDraw::DrawWeldSymbol* QGIWeldSymbol::getFeature(void)
{
    return m_weldFeat;
}

//preference
QColor QGIWeldSymbol::prefNormalColor()
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
                                        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/TechDraw/LeaderLines");
    App::Color fcColor;
    fcColor.setPackedValue(hGrp->GetUnsigned("Color", 0x00000000));
    m_colNormal = fcColor.asValue<QColor>();
    return m_colNormal;
}

double QGIWeldSymbol::prefArrowSize()
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter().
                                         GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/TechDraw/Dimensions");
    double size = Rez::guiX(hGrp->GetFloat("ArrowSize", 3.5));
    return size;
}


QRectF QGIWeldSymbol::boundingRect() const
{
    return childrenBoundingRect();
}

QPainterPath QGIWeldSymbol::shape() const
{
    return QGraphicsItemGroup::shape();
}

void QGIWeldSymbol::paint ( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget) {
    QStyleOptionGraphicsItem myOption(*option);
    myOption.state &= ~QStyle::State_Selected;

//    painter->setPen(Qt::red);
//    painter->drawRect(boundingRect());          //good for debugging

    QGIView::paint (painter, &myOption, widget);
}

#include <Mod/TechDraw/Gui/moc_QGIWeldSymbol.cpp>
