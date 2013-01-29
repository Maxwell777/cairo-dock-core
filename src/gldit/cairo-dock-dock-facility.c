/**
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <cairo.h>
#include <pango/pango.h>

#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <GL/gl.h> 
#include <GL/glu.h> 
#include <GL/glx.h> 

#include "cairo-dock-draw.h"
#include "cairo-dock-applications-manager.h"
#include "cairo-dock-image-buffer.h"
#include "cairo-dock-config.h"
#include "cairo-dock-module-factory.h"
#include "cairo-dock-callbacks.h"
#include "cairo-dock-icon-factory.h"
#include "cairo-dock-icon-facility.h"
#include "cairo-dock-separator-factory.h"
#include "cairo-dock-launcher-factory.h"
#include "cairo-dock-backends-manager.h"  // myBackendsParam.fSubDockSizeRatio
#include "cairo-dock-X-utilities.h"
#include "cairo-dock-log.h"
#include "cairo-dock-keyfile-utilities.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-dialog-manager.h"
#include "cairo-dock-notifications.h"
#include "cairo-dock-indicator-manager.h"  // myIndicators.bUseClassIndic
#include "cairo-dock-class-manager.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-X-manager.h"
#include "cairo-dock-global-variables.h"
#include "cairo-dock-data-renderer.h"
#include "cairo-dock-opengl.h"

#include "cairo-dock-dock-facility.h"

extern gboolean g_bUseOpenGL;  // for cairo_dock_make_preview()


void cairo_dock_update_dock_size (CairoDock *pDock)  // iMaxIconHeight et fFlatDockWidth doivent avoir ete mis a jour au prealable.
{
	g_return_if_fail (pDock != NULL);
	//g_print ("%s (%p, %d)\n", __func__, pDock, pDock->iRefCount);
	if (pDock->iSidUpdateDockSize != 0)
	{
		//g_print (" -> delayed\n");
		return;
	}
	int iPrevMaxDockHeight = pDock->iMaxDockHeight;
	int iPrevMaxDockWidth = pDock->iMaxDockWidth;
	
	//\__________________________ First compute the dock's size.
	
	// set the icons' size back to their default
	if (pDock->container.fRatio != 0)  // on remet leur taille reelle aux icones, sinon le calcul de max_dock_size sera biaise.
	{
		GList *ic;
		Icon *icon;
		pDock->fFlatDockWidth = -myIconsParam.iIconGap;
		pDock->iMaxIconHeight = 0;
		for (ic = pDock->icons; ic != NULL; ic = ic->next)
		{
			icon = ic->data;
			icon->fWidth /= pDock->container.fRatio;
			icon->fHeight /= pDock->container.fRatio;
			pDock->fFlatDockWidth += icon->fWidth + myIconsParam.iIconGap;
			if (! CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (icon))
				pDock->iMaxIconHeight = MAX (pDock->iMaxIconHeight, icon->fHeight);
		}
		if (pDock->iMaxIconHeight == 0)
			pDock->iMaxIconHeight = 10;
		pDock->container.fRatio = 1.;
	}
	
	// compute the size of the dock.
	pDock->iActiveWidth = pDock->iActiveHeight = 0;
	pDock->pRenderer->compute_size (pDock);
	if (pDock->iActiveWidth == 0)
		pDock->iActiveWidth = pDock->iMaxDockWidth;
	if (pDock->iActiveHeight == 0)
		pDock->iActiveHeight = pDock->iMaxDockHeight;
	
	// in case it's larger than the screen, iterate on the ratio until it fits the screen's width
	int iScreenHeight = gldi_dock_get_screen_height (pDock);
	double hmax = pDock->iMaxIconHeight;
	int iMaxAuthorizedWidth = cairo_dock_get_max_authorized_dock_width (pDock);
	int n = 0;  // counter to ensure we'll not loop forever.
	do
	{
		double fPrevRatio = pDock->container.fRatio;
		//g_print ("  %s (%d / %d)\n", __func__, (int)pDock->iMaxDockWidth, iMaxAuthorizedWidth);
		if (pDock->iMaxDockWidth > iMaxAuthorizedWidth)
		{
			pDock->container.fRatio *= (double)iMaxAuthorizedWidth / pDock->iMaxDockWidth;
		}
		else
		{
			double fMaxRatio = (pDock->iRefCount == 0 ? 1 : myBackendsParam.fSubDockSizeRatio);
			if (pDock->container.fRatio < fMaxRatio)
			{
				pDock->container.fRatio *= (double)iMaxAuthorizedWidth / pDock->iMaxDockWidth;
				pDock->container.fRatio = MIN (pDock->container.fRatio, fMaxRatio);
			}
			else
				pDock->container.fRatio = fMaxRatio;
		}
		
		if (pDock->iMaxDockHeight > iScreenHeight)
		{
			pDock->container.fRatio = MIN (pDock->container.fRatio, fPrevRatio * iScreenHeight / pDock->iMaxDockHeight);
		}
		
		if (fPrevRatio != pDock->container.fRatio)
		{
			//g_print ("  -> changement du ratio : %.3f -> %.3f (%d, %d try)\n", fPrevRatio, pDock->container.fRatio, pDock->iRefCount, n);
			Icon *icon;
			GList *ic;
			pDock->fFlatDockWidth = -myIconsParam.iIconGap;
			for (ic = pDock->icons; ic != NULL; ic = ic->next)
			{
				icon = ic->data;
				icon->fWidth *= pDock->container.fRatio / fPrevRatio;
				icon->fHeight *= pDock->container.fRatio / fPrevRatio;
				pDock->fFlatDockWidth += icon->fWidth + myIconsParam.iIconGap;
			}
			hmax *= pDock->container.fRatio / fPrevRatio;
			
			pDock->iActiveWidth = pDock->iActiveHeight = 0;
			pDock->pRenderer->compute_size (pDock);
			if (pDock->iActiveWidth == 0)
				pDock->iActiveWidth = pDock->iMaxDockWidth;
			if (pDock->iActiveHeight == 0)
				pDock->iActiveHeight = pDock->iMaxDockHeight;
		}
		
		//g_print ("*** ratio : %.3f -> %.3f\n", fPrevRatio, pDock->container.fRatio);
		n ++;
	} while ((pDock->iMaxDockWidth > iMaxAuthorizedWidth || pDock->iMaxDockHeight > iScreenHeight || (pDock->container.fRatio < 1 && pDock->iMaxDockWidth < iMaxAuthorizedWidth-5)) && n < 8);
	pDock->iMaxIconHeight = hmax;
	//g_print (">>> iMaxIconHeight : %d, ratio : %.2f, fFlatDockWidth : %.2f\n", (int) pDock->iMaxIconHeight, pDock->container.fRatio, pDock->fFlatDockWidth);
	
	//\__________________________ Then take the necessary actions due to the new size.
	// calculate the position of icons in the new frame.
	cairo_dock_calculate_dock_icons (pDock);
	
	// update the dock's shape.
	if (iPrevMaxDockHeight == pDock->iMaxDockHeight && iPrevMaxDockWidth == pDock->iMaxDockWidth)  // if the size has changed, shapes will be updated by the "configure" callback, so we don't need to do it here; if not, we do it in case the icons define a new shape (ex.: separators in Panel view) or in case the screen edge has changed.
	{
		cairo_dock_update_input_shape (pDock);  // done after the icons' position is known.
		switch (pDock->iInputState)  // update the input zone
		{
			case CAIRO_DOCK_INPUT_ACTIVE: cairo_dock_set_input_shape_active (pDock);;
			case CAIRO_DOCK_INPUT_AT_REST: cairo_dock_set_input_shape_at_rest (pDock);
			default: break;  // if hidden, nothing to do.
		}
	}
	
	if (iPrevMaxDockHeight == pDock->iMaxDockHeight && iPrevMaxDockWidth == pDock->iMaxDockWidth)  // same remark as for the input shape.
	{
		/// TODO: check that...
		///pDock->bWMIconsNeedUpdate = TRUE;
		cairo_dock_trigger_set_WM_icons_geometry (pDock);
	}
	
	// if the size has changed, move the dock to keep it centered.
	if (gldi_container_is_visible (CAIRO_CONTAINER (pDock)) && (iPrevMaxDockHeight != pDock->iMaxDockHeight || iPrevMaxDockWidth != pDock->iMaxDockWidth))
	{
		//g_print ("*******%s (%dx%d -> %dx%d)\n", __func__, iPrevMaxDockWidth, iPrevMaxDockHeight, pDock->iMaxDockWidth, pDock->iMaxDockHeight);
		cairo_dock_move_resize_dock (pDock);
	}
	
	// reload its background.
	cairo_dock_trigger_load_dock_background (pDock);
	
	// update the space reserved on the screen.
	if (pDock->iRefCount == 0 && pDock->iVisibility == CAIRO_DOCK_VISI_RESERVE)
		cairo_dock_reserve_space_for_dock (pDock, TRUE);
}

static gboolean _update_dock_size_idle (CairoDock *pDock)
{
	pDock->iSidUpdateDockSize = 0;
	cairo_dock_update_dock_size (pDock);
	gtk_widget_queue_draw (pDock->container.pWidget);
	return FALSE;
}
void cairo_dock_trigger_update_dock_size (CairoDock *pDock)
{
	if (pDock->iSidUpdateDockSize == 0)
	{
		pDock->iSidUpdateDockSize = g_idle_add ((GSourceFunc) _update_dock_size_idle, pDock);
	}
}

static void cairo_dock_manage_mouse_position (CairoDock *pDock)
{
	switch (pDock->iMousePositionType)
	{
		case CAIRO_DOCK_MOUSE_INSIDE :
			//g_print ("INSIDE (%d;%d;%d;%d;%d)\n", cairo_dock_entrance_is_allowed (pDock), pDock->iMagnitudeIndex, pDock->bIsGrowingUp, pDock->bIsShrinkingDown, pDock->iInputState);
			if (cairo_dock_entrance_is_allowed (pDock) && ((pDock->iMagnitudeIndex < CAIRO_DOCK_NB_MAX_ITERATIONS && ! pDock->bIsGrowingUp) || pDock->bIsShrinkingDown) && pDock->iInputState != CAIRO_DOCK_INPUT_HIDDEN && (pDock->iInputState != CAIRO_DOCK_INPUT_AT_REST || pDock->bIsDragging))  // on est dedans et la taille des icones est non maximale bien que le dock ne soit pas en train de grossir, cependant on respecte l'etat 'cache', et l'etat repos.
			{
				//g_print ("on est dedans en x et en y et la taille des icones est non maximale bien qu'aucune icone  ne soit animee (%d;%d)\n", pDock->iMagnitudeIndex, pDock->container.bInside);
				if (pDock->iRefCount != 0 && !pDock->container.bInside)
				{
					
					break;
				}
				//pDock->container.bInside = TRUE;
				///if ((pDock->bAtBottom && pDock->iRefCount == 0 && ! pDock->bAutoHide) || (pDock->container.iWidth != pDock->iMaxDockWidth || pDock->container.iHeight != pDock->iMaxDockHeight) || (!pDock->container.bInside))  // on le fait pas avec l'auto-hide, car un signal d'entree est deja emis a cause des mouvements/redimensionnements de la fenetre, et en rajouter un ici fout le boxon.  // !pDock->container.bInside ajoute pour le bug du chgt de bureau.
				if ((pDock->iMagnitudeIndex == 0 && pDock->iRefCount == 0 && ! pDock->bAutoHide && ! pDock->bIsGrowingUp) || !pDock->container.bInside)  // we are probably a little bit paranoia here, especially with the first case ... anyway, if we missed the 'enter' event for some reason, force it here.
				{
					//g_print ("  on emule une re-rentree (pDock->iMagnitudeIndex:%d)\n", pDock->iMagnitudeIndex);
					cairo_dock_emit_enter_signal (CAIRO_CONTAINER (pDock));
				}
				else // on se contente de faire grossir les icones.
				{
					//g_print ("  on se contente de faire grossir les icones\n");
					cairo_dock_start_growing (pDock);
					if (pDock->bAutoHide && pDock->iRefCount == 0)
						cairo_dock_start_showing (pDock);
				}
			}
		break ;

		case CAIRO_DOCK_MOUSE_ON_THE_EDGE :
			if (pDock->iMagnitudeIndex > 0 && ! pDock->bIsGrowingUp)
				cairo_dock_start_shrinking (pDock);
		break ;

		case CAIRO_DOCK_MOUSE_OUTSIDE :
			//g_print ("en dehors du dock (bIsShrinkingDown:%d;bIsGrowingUp:%d;iMagnitudeIndex:%d)\n", pDock->bIsShrinkingDown, pDock->bIsGrowingUp, pDock->iMagnitudeIndex);
			if (! pDock->bIsGrowingUp && ! pDock->bIsShrinkingDown && pDock->iSidLeaveDemand == 0 && pDock->iMagnitudeIndex > 0 && ! pDock->bIconIsFlyingAway)
			{
				if (pDock->iRefCount > 0)
				{
					Icon *pPointingIcon = cairo_dock_search_icon_pointing_on_dock (pDock, NULL);
					if (pPointingIcon && pPointingIcon->bPointed)  // sous-dock pointe, on le laisse en position haute.
						return;
				}
				//g_print ("on force a quitter (iRefCount:%d; bIsGrowingUp:%d; iMagnitudeIndex:%d)\n", pDock->iRefCount, pDock->bIsGrowingUp, pDock->iMagnitudeIndex);
				pDock->iSidLeaveDemand = g_timeout_add (MAX (myDocksParam.iLeaveSubDockDelay, 330), (GSourceFunc) cairo_dock_emit_leave_signal, (gpointer) pDock);
			}
		break ;
	}
}
Icon *cairo_dock_calculate_dock_icons (CairoDock *pDock)
{
	Icon *pPointedIcon = pDock->pRenderer->calculate_icons (pDock);
	cairo_dock_manage_mouse_position (pDock);
	if (1||pDock->iMousePositionType == CAIRO_DOCK_MOUSE_INSIDE)
	{
		return pPointedIcon;
	}
	else
	{
		if (pPointedIcon)
			pPointedIcon->bPointed = FALSE;
		return NULL;
	}
	///return (pDock->iMousePositionType == CAIRO_DOCK_MOUSE_INSIDE ? pPointedIcon : NULL);
}



  ////////////////////////////////
 /// WINDOW SIZE AND POSITION ///
////////////////////////////////

void cairo_dock_reserve_space_for_dock (CairoDock *pDock, gboolean bReserve)
{
	Window Xid = gldi_container_get_Xid (CAIRO_CONTAINER (pDock));
	int left=0, right=0, top=0, bottom=0;
	int left_start_y=0, left_end_y=0, right_start_y=0, right_end_y=0, top_start_x=0, top_end_x=0, bottom_start_x=0, bottom_end_x=0;

	if (bReserve)
	{
		int w = pDock->iMinDockWidth;
		int h = pDock->iMinDockHeight;
		int x, y;  // position qu'aurait la fenetre du dock s'il avait la taille minimale.
		cairo_dock_get_window_position_at_balance (pDock, w, h, &x, &y);
		//g_print ("%dx%d; %d;%d\n", w, h, x, y);
		
		if (pDock->container.bDirectionUp)
		{
			if (pDock->container.bIsHorizontal)
			{
				bottom = h + pDock->iGapY;
				bottom_start_x = x;
				bottom_end_x = x + w;
			}
			else
			{
				right = h + pDock->iGapY;
				right_start_y = x;
				right_end_y = x + w;
			}
		}
		else
		{
			if (pDock->container.bIsHorizontal)
			{
				top = h + pDock->iGapY;
				top_start_x = x;
				top_end_x = x + w;
			}
			else
			{
				left = h + pDock->iGapY;
				left_start_y = x;
				left_end_y = x + w;
			}
		}
	}
	
	cairo_dock_set_strut_partial (Xid, left, right, top, bottom, left_start_y, left_end_y, right_start_y, right_end_y, top_start_x, top_end_x, bottom_start_x, bottom_end_x);
}

void cairo_dock_prevent_dock_from_out_of_screen (CairoDock *pDock)
{
	int x, y;  // position du point invariant du dock.
	x = pDock->container.iWindowPositionX +  pDock->container.iWidth * pDock->fAlign;
	y = (pDock->container.bDirectionUp ? pDock->container.iWindowPositionY + pDock->container.iHeight : pDock->container.iWindowPositionY);
	//cd_debug ("%s (%d;%d)", __func__, x, y);
	
	int W = gldi_dock_get_screen_width (pDock), H = gldi_dock_get_screen_height (pDock);
	pDock->iGapX = x - W * pDock->fAlign;
	pDock->iGapY = (pDock->container.bDirectionUp ? H - y : y);
	//cd_debug (" -> (%d;%d)", pDock->iGapX, pDock->iGapY);
	
	if (pDock->iGapX < - W/2)
		pDock->iGapX = - W/2;
	if (pDock->iGapX > W/2)
		pDock->iGapX = W/2;
	if (pDock->iGapY < 0)
		pDock->iGapY = 0;
	if (pDock->iGapY > H)
		pDock->iGapY = H;
}

#define CD_VISIBILITY_MARGIN 20
void cairo_dock_get_window_position_at_balance (CairoDock *pDock, int iNewWidth, int iNewHeight, int *iNewPositionX, int *iNewPositionY)
{
	int W = gldi_dock_get_screen_width (pDock), H = gldi_dock_get_screen_height (pDock);
	int iWindowPositionX = (W - iNewWidth) * pDock->fAlign + pDock->iGapX;
	if (pDock->iRefCount == 0 && pDock->fAlign != .5)
		iWindowPositionX += (.5 - pDock->fAlign) * (pDock->iMaxDockWidth - iNewWidth);
	int iWindowPositionY = (pDock->container.bDirectionUp ? H - iNewHeight - pDock->iGapY : pDock->iGapY);
	//g_print ("pDock->iGapX : %d => iWindowPositionX <- %d\n", pDock->iGapX, iWindowPositionX);
	//g_print ("iNewHeight : %d -> pDock->container.iWindowPositionY <- %d\n", iNewHeight, iWindowPositionY);
	
	if (pDock->iRefCount == 0)
	{
		if (iWindowPositionX + iNewWidth < CD_VISIBILITY_MARGIN)
			iWindowPositionX = CD_VISIBILITY_MARGIN - iNewWidth;
		else if (iWindowPositionX > W - CD_VISIBILITY_MARGIN)
			iWindowPositionX = W - CD_VISIBILITY_MARGIN;
	}
	else
	{
		if (iWindowPositionX < - pDock->iLeftMargin)
			iWindowPositionX = - pDock->iLeftMargin;
		else if (iWindowPositionX > W - iNewWidth + pDock->iMinRightMargin)
			iWindowPositionX = W - iNewWidth + pDock->iMinRightMargin;
	}
	if (iWindowPositionY < - pDock->iMaxIconHeight)
		iWindowPositionY = - pDock->iMaxIconHeight;
	else if (iWindowPositionY > H - iNewHeight + pDock->iMaxIconHeight)
		iWindowPositionY = H - iNewHeight + pDock->iMaxIconHeight;
	
	int iScreenOffsetX = gldi_dock_get_screen_offset_x (pDock), iScreenOffsetY = gldi_dock_get_screen_offset_y (pDock);
	if (pDock->container.bIsHorizontal)
	{
		*iNewPositionX = iWindowPositionX + iScreenOffsetX;
		*iNewPositionY = iWindowPositionY + iScreenOffsetY;
	}
	else
	{
		*iNewPositionX = iWindowPositionX + iScreenOffsetX;
		*iNewPositionY = iWindowPositionY + iScreenOffsetY;
	}
	//g_print ("POSITION : %d+%d ; %d+%d\n", iWindowPositionX, pDock->iScreenOffsetX, iWindowPositionY, pDock->iScreenOffsetY);
}

static gboolean _move_resize_dock (CairoDock *pDock)
{
	int iNewWidth = pDock->iMaxDockWidth;
	int iNewHeight = pDock->iMaxDockHeight;
	int iNewPositionX, iNewPositionY;
	cairo_dock_get_window_position_at_balance (pDock, iNewWidth, iNewHeight, &iNewPositionX, &iNewPositionY);  // on ne peut pas intercepter le cas ou les nouvelles dimensions sont egales aux dimensions courantes de la fenetre, car il se peut qu'il y'ait 2 redimensionnements d'affilee s'annulant mutuellement (remove + insert d'une icone). Il faut donc avoir les 2 configure, sinon la taille reste bloquee aux valeurs fournies par le 1er configure.
	
	//g_print (" -> %dx%d, %d;%d\n", iNewWidth, iNewHeight, iNewPositionX, iNewPositionY);
	
	if (pDock->container.bIsHorizontal)
	{
		gdk_window_move_resize (gldi_container_get_gdk_window (CAIRO_CONTAINER (pDock)),
			iNewPositionX,
			iNewPositionY,
			iNewWidth,
			iNewHeight);  // lorsqu'on a 2 gdk_window_move_resize d'affilee, Compiz deconne et bloque le dock (il est toujours actif mais n'est plus redessine). Compiz envoit un configure de trop par rapport a Metacity.
	}
	else
	{
		gdk_window_move_resize (gldi_container_get_gdk_window (CAIRO_CONTAINER (pDock)),
			iNewPositionY,
			iNewPositionX,
			iNewHeight,
			iNewWidth);
	}
	pDock->iSidMoveResize = 0;
	return FALSE;
}

void cairo_dock_move_resize_dock (CairoDock *pDock)
{
	//g_print ("*********%s (current : %dx%d, %d;%d)\n", __func__, pDock->container.iWidth, pDock->container.iHeight, pDock->container.iWindowPositionX, pDock->container.iWindowPositionY);
	if (pDock->iSidMoveResize == 0)
	{
		pDock->iSidMoveResize = g_idle_add ((GSourceFunc)_move_resize_dock, pDock);
	}
	return ;
}


  ///////////////////
 /// INPUT SHAPE ///
///////////////////
static GldiShape *_cairo_dock_create_input_shape (CairoDock *pDock, int w, int h)
{
 	int W = pDock->iMaxDockWidth;
	int H = pDock->iMaxDockHeight;
	if (W == 0 || H == 0)  // very unlikely to happen, but anyway avoid this case.
	{
		return NULL;
	}
	
	double offset = (W - pDock->iActiveWidth) * pDock->fAlign + (pDock->iActiveWidth - w) / 2;
	
	GldiShape *pShapeBitmap;
	if (pDock->container.bIsHorizontal)
	{
		pShapeBitmap = gldi_container_create_input_shape (CAIRO_CONTAINER (pDock),
			///(W - w) * pDock->fAlign,
			offset,
			pDock->container.bDirectionUp ? H - h : 0,
			w,
			h);
	}
	else
	{
		pShapeBitmap = gldi_container_create_input_shape (CAIRO_CONTAINER (pDock),
			pDock->container.bDirectionUp ? H - h : 0,
			///(W - w) * pDock->fAlign,
			offset,
			h,
			w);
	}
	return pShapeBitmap;
}

void cairo_dock_update_input_shape (CairoDock *pDock)
{
	//\_______________ destroy the current input zones.
	if (pDock->pShapeBitmap != NULL)
	{
		gldi_shape_destroy (pDock->pShapeBitmap);
		pDock->pShapeBitmap = NULL;
	}
	if (pDock->pHiddenShapeBitmap != NULL)
	{
		gldi_shape_destroy (pDock->pHiddenShapeBitmap);
		pDock->pHiddenShapeBitmap = NULL;
	}
	if (pDock->pActiveShapeBitmap != NULL)
	{
		gldi_shape_destroy (pDock->pActiveShapeBitmap);
		pDock->pActiveShapeBitmap = NULL;
	}
	
	//\_______________ define the input zones' geometry
	int W = pDock->iMaxDockWidth;
	int H = pDock->iMaxDockHeight;
	int w = pDock->iMinDockWidth;
	int h = pDock->iMinDockHeight;
	//g_print ("%s (%dx%d; %dx%d)\n", __func__, w, h, W, H);
	///int w_ = MIN (myDocksParam.iVisibleZoneWidth, pDock->iMaxDockWidth);
	///int h_ = MIN (myDocksParam.iVisibleZoneHeight, pDock->iMaxDockHeight);
	int w_ = 1;
	int h_ = 1;
	
	//\_______________ check that the dock can have input zones.
	if (w == 0 || h == 0 || pDock->iRefCount > 0 || W == 0 || H == 0)
	{
		if (pDock->iActiveWidth != pDock->iMaxDockWidth || pDock->iActiveHeight != pDock->iMaxDockHeight)  // else all the dock is active when the mouse is inside, so we can just set a NULL shape.
			pDock->pActiveShapeBitmap = _cairo_dock_create_input_shape (pDock, pDock->iActiveWidth, pDock->iActiveHeight);
		if (pDock->iInputState != CAIRO_DOCK_INPUT_ACTIVE)
		{
			//g_print ("+++ input shape active on update input shape\n");
			cairo_dock_set_input_shape_active (pDock);
			pDock->iInputState = CAIRO_DOCK_INPUT_ACTIVE;
		}
		return ;
	}
	
	//\_______________ create the input zones based on the previous geometries.
	pDock->pShapeBitmap = _cairo_dock_create_input_shape (pDock, w, h);
	
	pDock->pHiddenShapeBitmap = _cairo_dock_create_input_shape (pDock, w_, h_);
	
	if (pDock->iActiveWidth != pDock->iMaxDockWidth || pDock->iActiveHeight != pDock->iMaxDockHeight)  // else all the dock is active when the mouse is inside, so we can just set a NULL shape.
		pDock->pActiveShapeBitmap = _cairo_dock_create_input_shape (pDock, pDock->iActiveWidth, pDock->iActiveHeight);
	
	//\_______________ if the renderer can define the input shape, let it finish the job.
	if (pDock->pRenderer->update_input_shape != NULL)
		pDock->pRenderer->update_input_shape (pDock);
}



  ///////////////////
 /// LINEAR DOCK ///
///////////////////

void cairo_dock_calculate_icons_positions_at_rest_linear (GList *pIconList, double fFlatDockWidth)
{
	//g_print ("%s (%d, +%d)\n", __func__, fFlatDockWidth);
	double x_cumulated = 0;
	GList* ic;
	Icon *icon;
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;

		if (x_cumulated + icon->fWidth / 2 < 0)
			icon->fXAtRest = x_cumulated + fFlatDockWidth;
		else if (x_cumulated + icon->fWidth / 2 > fFlatDockWidth)
			icon->fXAtRest = x_cumulated - fFlatDockWidth;
		else
			icon->fXAtRest = x_cumulated;
		//g_print ("%s : fXAtRest = %.2f\n", icon->cName, icon->fXAtRest);

		x_cumulated += icon->fWidth + myIconsParam.iIconGap;
	}
}

double cairo_dock_calculate_max_dock_width (CairoDock *pDock, double fFlatDockWidth, double fWidthConstraintFactor, double fExtraWidth)
{
	double fMaxDockWidth = 0.;
	//g_print ("%s (%d)\n", __func__, (int)fFlatDockWidth);
	GList *pIconList = pDock->icons;
	if (pIconList == NULL)
		return 2 * myDocksParam.iDockRadius + myDocksParam.iDockLineWidth + 2 * myDocksParam.iFrameMargin;

	//\_______________ On remet a zero les positions extremales des icones.
	GList* ic;
	Icon *icon;
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		icon->fXMax = -1e4;
		icon->fXMin = 1e4;
	}

	//\_______________ On simule le passage du curseur sur toute la largeur du dock, et on chope la largeur maximale qui s'en degage, ainsi que les positions d'equilibre de chaque icone.
	GList *ic2;
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;

		cairo_dock_calculate_wave_with_position_linear (pIconList, icon->fXAtRest, pDock->fMagnitudeMax, fFlatDockWidth, 0, 0, 0.5, 0, pDock->container.bDirectionUp);
		
		for (ic2 = pIconList; ic2 != NULL; ic2 = ic2->next)
		{
			icon = ic2->data;

			if (icon->fX + icon->fWidth * icon->fScale > icon->fXMax)
				icon->fXMax = icon->fX + icon->fWidth * icon->fScale;
			if (icon->fX < icon->fXMin)
				icon->fXMin = icon->fX;
		}
	}
	cairo_dock_calculate_wave_with_position_linear (pIconList, fFlatDockWidth - 1, pDock->fMagnitudeMax, fFlatDockWidth, 0, 0, pDock->fAlign, 0, pDock->container.bDirectionUp);  // last calculation at the extreme right of the dock.
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;

		if (icon->fX + icon->fWidth * icon->fScale > icon->fXMax)
			icon->fXMax = icon->fX + icon->fWidth * icon->fScale;
		if (icon->fX < icon->fXMin)
			icon->fXMin = icon->fX;
	}

	fMaxDockWidth = (icon->fXMax - ((Icon *) pIconList->data)->fXMin) * fWidthConstraintFactor + fExtraWidth;
	fMaxDockWidth = ceil (fMaxDockWidth) + 1;

	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		icon->fXMin += fMaxDockWidth / 2;
		icon->fXMax += fMaxDockWidth / 2;
		//g_print ("%s : [%d;%d]\n", icon->cName, (int) icon->fXMin, (int) icon->fXMax);
		icon->fX = icon->fXAtRest;
		icon->fScale = 1;
	}

	return fMaxDockWidth;
}

Icon * cairo_dock_calculate_wave_with_position_linear (GList *pIconList, int x_abs, gdouble fMagnitude, double fFlatDockWidth, int iWidth, int iHeight, double fAlign, double fFoldingFactor, gboolean bDirectionUp)
{
	//g_print (">>>>>%s (%d/%.2f, %dx%d, %.2f, %.2f)\n", __func__, x_abs, fFlatDockWidth, iWidth, iHeight, fAlign, fFoldingFactor);
	if (pIconList == NULL)
		return NULL;
	if (x_abs < 0 && iWidth > 0)  // ces cas limite sont la pour empecher les icones de retrecir trop rapidement quand on sort par les cotes.
		///x_abs = -1;
		x_abs = 0;
	else if (x_abs > fFlatDockWidth && iWidth > 0)
		///x_abs = fFlatDockWidth+1;
		x_abs = (int) fFlatDockWidth;
	
	
	float x_cumulated = 0, fXMiddle, fDeltaExtremum;
	GList* ic, *pointed_ic;
	Icon *icon, *prev_icon;
	double fScale = 0.;
	double offset = 0.;
	pointed_ic = (x_abs < 0 ? pIconList : NULL);
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		x_cumulated = icon->fXAtRest;
		fXMiddle = icon->fXAtRest + icon->fWidth / 2;

		//\_______________ On calcule sa phase (pi/2 au niveau du curseur).
		icon->fPhase = (fXMiddle - x_abs) / myIconsParam.iSinusoidWidth * G_PI + G_PI / 2;
		if (icon->fPhase < 0)
		{
			icon->fPhase = 0;
		}
		else if (icon->fPhase > G_PI)
		{
			icon->fPhase = G_PI;
		}
		
		//\_______________ On en deduit l'amplitude de la sinusoide au niveau de cette icone, et donc son echelle.
		icon->fScale = 1 + fMagnitude * myIconsParam.fAmplitude * sin (icon->fPhase);
		if (iWidth > 0 && icon->fInsertRemoveFactor != 0)
		{
			fScale = icon->fScale;
			///offset += (icon->fWidth * icon->fScale) * (pointed_ic == NULL ? 1 : -1);
			if (icon->fInsertRemoveFactor > 0)
				icon->fScale *= icon->fInsertRemoveFactor;
			else
				icon->fScale *= (1 + icon->fInsertRemoveFactor);
			///offset -= (icon->fWidth * icon->fScale) * (pointed_ic == NULL ? 1 : -1);
		}
		
		icon->fY = (bDirectionUp ? iHeight - myDocksParam.iDockLineWidth - myDocksParam.iFrameMargin - icon->fScale * icon->fHeight : myDocksParam.iDockLineWidth + myDocksParam.iFrameMargin);
		//g_print ("%s fY : %d; %.2f\n", icon->cName, iHeight, icon->fHeight);
		
		//\_______________ Si on avait deja defini l'icone pointee, on peut placer l'icone courante par rapport a la precedente.
		if (pointed_ic != NULL)
		{
			if (ic == pIconList)  // peut arriver si on est en dehors a gauche du dock.
			{
				icon->fX = x_cumulated - 1. * (fFlatDockWidth - iWidth) / 2;
				//g_print ("  en dehors a gauche : icon->fX = %.2f (%.2f)\n", icon->fX, x_cumulated);
			}
			else
			{
				prev_icon = (ic->prev != NULL ? ic->prev->data : cairo_dock_get_last_icon (pIconList));
				icon->fX = prev_icon->fX + (prev_icon->fWidth + myIconsParam.iIconGap) * prev_icon->fScale;

				if (icon->fX + icon->fWidth * icon->fScale > icon->fXMax - myIconsParam.fAmplitude * fMagnitude * (icon->fWidth + 1.5*myIconsParam.iIconGap) / 8 && iWidth != 0)
				{
					//g_print ("  on contraint %s (fXMax=%.2f , fX=%.2f\n", prev_icon->cName, prev_icon->fXMax, prev_icon->fX);
					fDeltaExtremum = icon->fX + icon->fWidth * icon->fScale - (icon->fXMax - myIconsParam.fAmplitude * fMagnitude * (icon->fWidth + 1.5*myIconsParam.iIconGap) / 16);
					if (myIconsParam.fAmplitude != 0)
						icon->fX -= fDeltaExtremum * (1 - (icon->fScale - 1) / myIconsParam.fAmplitude) * fMagnitude;
				}
			}
			icon->fX = fAlign * iWidth + (icon->fX - fAlign * iWidth) * (1. - fFoldingFactor);
			//g_print ("  a droite : icon->fX = %.2f (%.2f)\n", icon->fX, x_cumulated);
		}
		
		//\_______________ On regarde si on pointe sur cette icone.
		if (x_cumulated + icon->fWidth + .5*myIconsParam.iIconGap >= x_abs && x_cumulated - .5*myIconsParam.iIconGap <= x_abs && pointed_ic == NULL)  // on a trouve l'icone sur laquelle on pointe.
		{
			pointed_ic = ic;
			///icon->bPointed = TRUE;
			icon->bPointed = (x_abs != (int) fFlatDockWidth && x_abs != 0);
			icon->fX = x_cumulated - (fFlatDockWidth - iWidth) / 2 + (1 - icon->fScale) * (x_abs - x_cumulated + .5*myIconsParam.iIconGap);
			icon->fX = fAlign * iWidth + (icon->fX - fAlign * iWidth) * (1. - fFoldingFactor);
			//g_print ("  icone pointee : fX = %.2f (%.2f, %d)\n", icon->fX, x_cumulated, icon->bPointed);
		}
		else
			icon->bPointed = FALSE;
		
		if (iWidth > 0 && icon->fInsertRemoveFactor != 0)
		{
			if (pointed_ic != ic)  // bPointed peut etre false a l'extremite droite.
				offset += (icon->fWidth * (fScale - icon->fScale)) * (pointed_ic == NULL ? 1 : -1);
			else
				offset += (2*(fXMiddle - x_abs) * (fScale - icon->fScale)) * (pointed_ic == NULL ? 1 : -1);
		}
	}
	
	//\_______________ On place les icones precedant l'icone pointee par rapport a celle-ci.
	if (pointed_ic == NULL)  // on est a droite des icones.
	{
		pointed_ic = g_list_last (pIconList);
		icon = pointed_ic->data;
		icon->fX = x_cumulated - (fFlatDockWidth - iWidth) / 2 + (1 - icon->fScale) * (icon->fWidth + .5*myIconsParam.iIconGap);
		icon->fX = fAlign * iWidth + (icon->fX - fAlign * iWidth) * (1 - fFoldingFactor);
		//g_print ("  en dehors a droite : icon->fX = %.2f (%.2f)\n", icon->fX, x_cumulated);
	}
	
	ic = pointed_ic;
	while (ic != pIconList)
	{
		icon = ic->data;
		
		ic = ic->prev;  // since ic != pIconList, ic->prev is not NULL
		prev_icon = ic->data;
		
		prev_icon->fX = icon->fX - (prev_icon->fWidth + myIconsParam.iIconGap) * prev_icon->fScale;
		//g_print ("fX <- %.2f; fXMin : %.2f\n", prev_icon->fX, prev_icon->fXMin);
		if (prev_icon->fX < prev_icon->fXMin + myIconsParam.fAmplitude * fMagnitude * (prev_icon->fWidth + 1.5*myIconsParam.iIconGap) / 8 && iWidth != 0 && x_abs < iWidth && fMagnitude > 0)  /// && prev_icon->fPhase == 0   // on rajoute 'fMagnitude > 0' sinon il y'a un leger "saut" du aux contraintes a gauche de l'icone pointee.
		{
			//g_print ("  on contraint %s (fXMin=%.2f , fX=%.2f\n", prev_icon->cName, prev_icon->fXMin, prev_icon->fX);
			fDeltaExtremum = prev_icon->fX - (prev_icon->fXMin + myIconsParam.fAmplitude * fMagnitude * (prev_icon->fWidth + 1.5*myIconsParam.iIconGap) / 16);
			if (myIconsParam.fAmplitude != 0)
				prev_icon->fX -= fDeltaExtremum * (1 - (prev_icon->fScale - 1) / myIconsParam.fAmplitude) * fMagnitude;
		}
		prev_icon->fX = fAlign * iWidth + (prev_icon->fX - fAlign * iWidth) * (1. - fFoldingFactor);
		//g_print ("  prev_icon->fX : %.2f\n", prev_icon->fX);
	}
	
	if (offset != 0)
	{
		offset /= 2;
		//g_print ("offset : %.2f (pointed:%s)\n", offset, pointed_ic?((Icon*)pointed_ic->data)->cName:"none");
		for (ic = pIconList; ic != NULL; ic = ic->next)
		{
			icon = ic->data;
			//if (ic == pIconList)
			//	cd_debug ("fX : %.2f - %.2f", icon->fX, offset);
			icon->fX -= offset;
		}
	}
	
	icon = pointed_ic->data;
	return (icon->bPointed ? icon : NULL);
}

Icon *cairo_dock_apply_wave_effect_linear (CairoDock *pDock)
{
	//\_______________ On calcule la position du curseur dans le referentiel du dock a plat.
	//int dx = pDock->container.iMouseX - (pDock->iOffsetForExtend * (pDock->fAlign - .5) * 2) - pDock->container.iWidth / 2;  // ecart par rapport au milieu du dock a plat.
	//int x_abs = dx + pDock->fFlatDockWidth / 2;  // ecart par rapport a la gauche du dock minimal  plat.
	//g_print ("%s (flat:%d, w:%d, x:%d)\n", __func__, (int)pDock->fFlatDockWidth, pDock->container.iWidth, pDock->container.iMouseX);
	double offset = (pDock->container.iWidth - pDock->iActiveWidth) * pDock->fAlign + (pDock->iActiveWidth - pDock->fFlatDockWidth) / 2;
	int x_abs = pDock->container.iMouseX - offset;
	//\_______________ On calcule l'ensemble des parametres des icones.
	double fMagnitude = cairo_dock_calculate_magnitude (pDock->iMagnitudeIndex);  // * pDock->fMagnitudeMax
	Icon *pPointedIcon = cairo_dock_calculate_wave_with_position_linear (pDock->icons, x_abs, fMagnitude, pDock->fFlatDockWidth, pDock->container.iWidth, pDock->container.iHeight, pDock->fAlign, pDock->fFoldingFactor, pDock->container.bDirectionUp);  // iMaxDockWidth
	return pPointedIcon;
}

double cairo_dock_get_current_dock_width_linear (CairoDock *pDock)
{
	if (pDock->icons == NULL)
		//return 2 * myDocksParam.iDockRadius + myDocksParam.iDockLineWidth + 2 * myDocksParam.iFrameMargin;
		return 1 + 2 * myDocksParam.iFrameMargin;

	Icon *pLastIcon = cairo_dock_get_last_icon (pDock->icons);
	Icon *pFirstIcon = cairo_dock_get_first_icon (pDock->icons);
	double fWidth = pLastIcon->fX - pFirstIcon->fX + pLastIcon->fWidth * pLastIcon->fScale + 2 * myDocksParam.iFrameMargin;  //  + 2 * myDocksParam.iDockRadius + myDocksParam.iDockLineWidth + 2 * myDocksParam.iFrameMargin

	return fWidth;
}

void cairo_dock_check_if_mouse_inside_linear (CairoDock *pDock)
{
	CairoDockMousePositionType iMousePositionType;
	int iWidth = pDock->container.iWidth;
	///int iHeight = (pDock->fMagnitudeMax != 0 ? pDock->container.iHeight : pDock->iMinDockHeight);
	int iHeight = pDock->iActiveHeight;
	///int iExtraHeight = (pDock->bAtBottom ? 0 : myIconsParam.iLabelSize);
	// int iExtraHeight = 0;  /// il faudrait voir si on a un sous-dock ou un dialogue au dessus :-/
	int iMouseX = pDock->container.iMouseX;
	int iMouseY = (pDock->container.bDirectionUp ? pDock->container.iHeight - pDock->container.iMouseY : pDock->container.iMouseY);
	//g_print ("%s (%dx%d, %dx%d, %f)\n", __func__, iMouseX, iMouseY, iWidth, iHeight, pDock->fFoldingFactor);

	//\_______________ On regarde si le curseur est dans le dock ou pas, et on joue sur la taille des icones en consequence.
	double offset = (iWidth - pDock->iActiveWidth) * pDock->fAlign + (pDock->iActiveWidth - pDock->fFlatDockWidth) / 2;
	int x_abs = pDock->container.iMouseX - offset;
	///int x_abs = pDock->container.iMouseX + (pDock->fFlatDockWidth - iWidth) * pDock->fAlign;  // abscisse par rapport a la gauche du dock minimal plat.
	gboolean bMouseInsideDock = (x_abs >= 0 && x_abs <= pDock->fFlatDockWidth && iMouseX > 0 && iMouseX < iWidth);
	//g_print ("bMouseInsideDock : %d (%d;%d/%.2f)\n", bMouseInsideDock, pDock->container.bInside, x_abs, pDock->fFlatDockWidth);

	if (! bMouseInsideDock)  // hors du dock par les cotes.
	{
		if (/*cairo_dock_is_extended_dock (pDock) && */pDock->bAutoHide)  // c'est penible de sortir du dock trop facilement avec l'auto-hide.
		{
			if (iMouseY >= 0 && iMouseY < iHeight)
				iMousePositionType = CAIRO_DOCK_MOUSE_ON_THE_EDGE;
			else
				iMousePositionType = CAIRO_DOCK_MOUSE_OUTSIDE;
		}
		else
		{
			/**double fSideMargin = fabs (pDock->fAlign - .5) * (iWidth - pDock->fFlatDockWidth);
			if (x_abs < - fSideMargin || x_abs > pDock->fFlatDockWidth + fSideMargin)
				iMousePositionType = CAIRO_DOCK_MOUSE_OUTSIDE;
			else
				iMousePositionType = CAIRO_DOCK_MOUSE_ON_THE_EDGE;*/
			if (iMouseY >= 0 && iMouseY < iHeight)
				iMousePositionType = CAIRO_DOCK_MOUSE_ON_THE_EDGE;
			else
				iMousePositionType = CAIRO_DOCK_MOUSE_OUTSIDE;
		}
	}
	else if (iMouseY >= 0 && iMouseY < iHeight)  // et en plus on est dedans en y.  //  && pPointedIcon != NULL
	{
		//g_print ("on est dedans en x et en y (iMouseX=%d => x_abs=%d ; iMouseY=%d/%d)\n", iMouseX, x_abs, iMouseY, iHeight);
		//pDock->container.bInside = TRUE;
		iMousePositionType = CAIRO_DOCK_MOUSE_INSIDE;
	}
	else
		iMousePositionType = CAIRO_DOCK_MOUSE_OUTSIDE;
	
	pDock->iMousePositionType = iMousePositionType;
}


#define make_icon_avoid_mouse(icon, sens) do { \
	cairo_dock_mark_icon_as_avoiding_mouse (icon);\
	icon->fAlpha = 0.75;\
	if (myIconsParam.fAmplitude != 0)\
		icon->fDrawX += icon->fWidth * icon->fScale / 4 * sens; } while (0)
static inline gboolean _cairo_dock_check_can_drop_linear (CairoDock *pDock, CairoDockIconGroup iGroup, double fMargin)
{
	gboolean bCanDrop = FALSE;
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon->bPointed)
		{
			cd_debug ("icon->fWidth: %d, %.2f", (int)icon->fWidth, icon->fScale);
			cd_debug ("x: %d / %d", pDock->container.iMouseX, (int)icon->fDrawX);
			if (pDock->container.iMouseX < icon->fDrawX + icon->fWidth * icon->fScale * fMargin)  // on est a gauche.  // fDrawXAtRest
			{
				Icon *prev_icon = (ic->prev ? ic->prev->data : NULL);
				if (icon->iGroup == iGroup || (prev_icon && prev_icon->iGroup == iGroup))
				{
					make_icon_avoid_mouse (icon, 1);
					if (prev_icon)
						make_icon_avoid_mouse (prev_icon, -1);
					//g_print ("%s> <%s\n", prev_icon->cName, icon->cName);
					bCanDrop = TRUE;
				}
			}
			else if (pDock->container.iMouseX > icon->fDrawX + icon->fWidth * icon->fScale * (1 - fMargin))  // on est a droite.  // fDrawXAtRest
			{
				Icon *next_icon = (ic->next ? ic->next->data : NULL);
				if (icon->iGroup == iGroup || (next_icon && next_icon->iGroup == iGroup))
				{
					make_icon_avoid_mouse (icon, -1);
					if (next_icon)
						make_icon_avoid_mouse (next_icon, 1);
					//g_print ("%s> <%s\n", icon->cName, next_icon->cName);
					bCanDrop = TRUE;
				}
				ic = ic->next;  // on la saute.
				if (ic == NULL)
					break;
			}  // else on est dessus.
		}
		else
			cairo_dock_stop_marking_icon_as_avoiding_mouse (icon);
	}
	
	return bCanDrop;
}


void cairo_dock_check_can_drop_linear (CairoDock *pDock)
{
	if (! pDock->bIsDragging)  // not dragging, so no drop possible.
	{
		pDock->bCanDrop = FALSE;
	}
	else if (pDock->icons == NULL)  // dragging but no icons, so drop always possible.
	{
		pDock->bCanDrop = TRUE;
	}
	else  // dragging and some icons.
	{
		pDock->bCanDrop = _cairo_dock_check_can_drop_linear (pDock, pDock->iAvoidingMouseIconType, pDock->fAvoidingMouseMargin);
	}
}

void cairo_dock_stop_marking_icons (CairoDock *pDock)
{
	if (pDock->icons == NULL)
		return;
	//g_print ("%s (%d)\n", __func__, iType);

	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		cairo_dock_stop_marking_icon_as_avoiding_mouse (icon);
	}
}


void cairo_dock_set_subdock_position_linear (Icon *pPointedIcon, CairoDock *pDock)
{
	CairoDock *pSubDock = pPointedIcon->pSubDock;
	///int iX = pPointedIcon->fXAtRest - (pDock->fFlatDockWidth - pDock->iMaxDockWidth) / 2 + pPointedIcon->fWidth / 2 + (pDock->iOffsetForExtend * (pDock->fAlign - .5) * 2);
	//int iX = pPointedIcon->fDrawX + pPointedIcon->fWidth * pPointedIcon->fScale / 2 + (pDock->iOffsetForExtend * (pDock->fAlign - .5) * 2);
	int iX = pPointedIcon->fDrawX + pPointedIcon->fWidth * pPointedIcon->fScale / 2;
	if (pSubDock->container.bIsHorizontal == pDock->container.bIsHorizontal)
	{
		pSubDock->fAlign = 0.5;
		pSubDock->iGapX = iX + pDock->container.iWindowPositionX - gldi_dock_get_screen_offset_x (pDock) - gldi_dock_get_screen_width (pDock) / 2;  // ici les sous-dock ont un alignement egal a 0.5
		pSubDock->iGapY = pDock->iGapY + pDock->iActiveHeight;
	}
	else
	{
		pSubDock->fAlign = (pDock->container.bDirectionUp ? 1 : 0);
		pSubDock->iGapX = (pDock->iGapY + pDock->iActiveHeight) * (pDock->container.bDirectionUp ? -1 : 1);
		if (pDock->container.bDirectionUp)
			pSubDock->iGapY = gldi_dock_get_screen_width (pDock) - (iX + pDock->container.iWindowPositionX - gldi_dock_get_screen_offset_x (pDock)) - pSubDock->iMaxDockHeight / 2;  // les sous-dock ont un alignement egal a 1.
		else
			pSubDock->iGapY = iX + pDock->container.iWindowPositionX - pSubDock->iMaxDockHeight / 2;  // les sous-dock ont un alignement egal a 0.
	}
}


GList *cairo_dock_get_first_drawn_element_linear (GList *icons)
{
	Icon *icon;
	GList *ic;
	GList *pFirstDrawnElement = NULL;
	for (ic = icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon->bPointed)
			break ;
	}
	
	if (ic == NULL || ic->next == NULL)  // derniere icone ou aucune pointee.
		pFirstDrawnElement = icons;
	else
		pFirstDrawnElement = ic->next;
	return pFirstDrawnElement;
}


void cairo_dock_show_subdock (Icon *pPointedIcon, CairoDock *pParentDock)
{
	cd_debug ("on montre le dock fils");
	CairoDock *pSubDock = pPointedIcon->pSubDock;
	g_return_if_fail (pSubDock != NULL);
	
	if (gldi_container_is_visible (CAIRO_CONTAINER (pSubDock)))  // already visible.
	{
		if (pSubDock->bIsShrinkingDown)  // il est en cours de diminution, on renverse le processus.
		{
			cairo_dock_start_growing (pSubDock);
		}
		return ;
	}
	
	// place the sub-dock
	pSubDock->pRenderer->set_subdock_position (pPointedIcon, pParentDock);
	
	int iNewWidth = pSubDock->iMaxDockWidth;
	int iNewHeight = pSubDock->iMaxDockHeight;
	int iNewPositionX, iNewPositionY;
	cairo_dock_get_window_position_at_balance (pSubDock, iNewWidth, iNewHeight, &iNewPositionX, &iNewPositionY);
	
	gtk_window_present (GTK_WINDOW (pSubDock->container.pWidget));
	
	if (pSubDock->container.bIsHorizontal)
	{
		gdk_window_move_resize (gldi_container_get_gdk_window (CAIRO_CONTAINER (pSubDock)),
			iNewPositionX,
			iNewPositionY,
			iNewWidth,
			iNewHeight);
	}
	else
	{
		gdk_window_move_resize (gldi_container_get_gdk_window (CAIRO_CONTAINER (pSubDock)),
			iNewPositionY,
			iNewPositionX,
			iNewHeight,
			iNewWidth);
		if (myIconsParam.bTextAlwaysHorizontal)  // in this case, the sub-dock is over the label, so this one is drawn with a low transparency, so we trigger the redraw.
			gtk_widget_queue_draw (pParentDock->container.pWidget);
	}
	
	// animate it
	if (myDocksParam.bAnimateSubDock && pSubDock->icons != NULL)
	{
		pSubDock->fFoldingFactor = .99;
		cairo_dock_start_growing (pSubDock);  // on commence a faire grossir les icones.
		pSubDock->pRenderer->calculate_icons (pSubDock);  // on recalcule les icones car sinon le 1er dessin se fait avec les parametres tels qu'ils etaient lorsque le dock s'est cache; or l'animation de pliage peut prendre plus de temps que celle de cachage.
	}
	else
	{
		pSubDock->fFoldingFactor = 0;
		///gtk_widget_queue_draw (pSubDock->container.pWidget);
	}
	cairo_dock_notify_on_object (pPointedIcon, NOTIFICATION_UNFOLD_SUBDOCK, pPointedIcon);
	
	cairo_dock_replace_all_dialogs ();
}



static gboolean _redraw_subdock_content_idle (Icon *pIcon)
{
	CairoDock *pDock = cairo_dock_search_dock_from_name (pIcon->cParentDockName);
	if (pDock != NULL)
	{
		if (pIcon->pSubDock != NULL)
		{
			if (pDock->container.iWidth == 1 && pDock->container.iHeight == 1)  // if the GTK resize process has not yet ended, the window may be in an awkward state: the size is forced by GTK to 1x1, which causes the FBO to fail (obviously, 1x1 is too small).
			{
				pIcon->iSidRedrawSubdockContent = g_idle_add ((GSourceFunc) _redraw_subdock_content_idle, pIcon);
				return FALSE;
			}
			cairo_dock_draw_subdock_content_on_icon (pIcon, pDock);
		}
		else  // l'icone a pu perdre son sous-dock entre-temps (exemple : une classe d'appli contenant 2 icones, dont on enleve l'une des 2.
		{
			cairo_dock_reload_icon_image (pIcon, CAIRO_CONTAINER (pDock));
		}
		cairo_dock_redraw_icon (pIcon, CAIRO_CONTAINER (pDock));
	}
	pIcon->iSidRedrawSubdockContent = 0;
	return FALSE;
}
void cairo_dock_trigger_redraw_subdock_content (CairoDock *pDock)
{
	Icon *pPointingIcon = cairo_dock_search_icon_pointing_on_dock (pDock, NULL);
	//g_print ("%s (%s, %d)\n", __func__, pPointingIcon?pPointingIcon->cName:NULL, pPointingIcon?pPointingIcon->iSubdockViewType:0);
	if (pPointingIcon != NULL && (pPointingIcon->iSubdockViewType != 0 || (pPointingIcon->cClass != NULL && ! myIndicatorsParam.bUseClassIndic && (CAIRO_DOCK_ICON_TYPE_IS_CLASS_CONTAINER (pPointingIcon) || CAIRO_DOCK_ICON_TYPE_IS_LAUNCHER (pPointingIcon)))))
	{
		if (pPointingIcon->iSidRedrawSubdockContent != 0)  // s'il y'a deja un redessin de prevu, on le passe a la fin de facon a ce qu'il ne se fasse  pas avant le redessin de l'icone responsable de ce trigger.
			g_source_remove (pPointingIcon->iSidRedrawSubdockContent);
		pPointingIcon->iSidRedrawSubdockContent = g_idle_add ((GSourceFunc) _redraw_subdock_content_idle, pPointingIcon);
	}
}

void cairo_dock_trigger_redraw_subdock_content_on_icon (Icon *icon)
{
	if (icon->iSidRedrawSubdockContent != 0)
		g_source_remove (icon->iSidRedrawSubdockContent);
	icon->iSidRedrawSubdockContent = g_idle_add ((GSourceFunc) _redraw_subdock_content_idle, icon);
}

void cairo_dock_redraw_subdock_content (CairoDock *pDock)
{
	CairoDock *pParentDock = NULL;
	Icon *pPointingIcon = cairo_dock_search_icon_pointing_on_dock (pDock, &pParentDock);
	if (pPointingIcon != NULL && pPointingIcon->iSubdockViewType != 0 && pPointingIcon->iSidRedrawSubdockContent == 0 && pParentDock != NULL)
	{
		cairo_dock_draw_subdock_content_on_icon (pPointingIcon, pParentDock);
		cairo_dock_redraw_icon (pPointingIcon, CAIRO_CONTAINER (pParentDock));
	}
}

static gboolean _update_WM_icons (CairoDock *pDock)
{
	cairo_dock_set_icons_geometry_for_window_manager (pDock);
	pDock->iSidUpdateWMIcons = 0;
	return FALSE;
}
void cairo_dock_trigger_set_WM_icons_geometry (CairoDock *pDock)
{
	if (pDock->iSidUpdateWMIcons == 0)
	{
		pDock->iSidUpdateWMIcons = g_idle_add ((GSourceFunc) _update_WM_icons, pDock);
	}
}

void cairo_dock_resize_icon_in_dock (Icon *pIcon, CairoDock *pDock)  // resize the icon according to the requested size previously set on the icon.
{
	cairo_dock_set_icon_size_in_dock (pDock, pIcon);
	
	cairo_dock_load_icon_image (pIcon, CAIRO_CONTAINER (pDock));  // handles the applet's context
	
	if (cairo_dock_get_icon_data_renderer (pIcon) != NULL)
		cairo_dock_reload_data_renderer_on_icon (pIcon, CAIRO_CONTAINER (pDock));
	
	cairo_dock_trigger_update_dock_size (pDock);
	gtk_widget_queue_draw (pDock->container.pWidget);
}


  ///////////////////////
 /// DOCK BACKGROUND ///
///////////////////////

static cairo_surface_t *_cairo_dock_make_stripes_background (int iWidth, int iHeight, double *fStripesColorBright, double *fStripesColorDark, int iNbStripes, double fStripesWidth, double fStripesAngle)
{
	cairo_pattern_t *pStripesPattern;
	if (fabs (fStripesAngle) != 90)
		pStripesPattern = cairo_pattern_create_linear (0.0f,
			0.0f,
			iWidth,
			iWidth * tan (fStripesAngle * G_PI/180.));
	else
		pStripesPattern = cairo_pattern_create_linear (0.0f,
			0.0f,
			0.,
			(fStripesAngle == 90) ? iHeight : - iHeight);
	g_return_val_if_fail (cairo_pattern_status (pStripesPattern) == CAIRO_STATUS_SUCCESS, NULL);

	cairo_pattern_set_extend (pStripesPattern, CAIRO_EXTEND_REPEAT);

	if (iNbStripes > 0)
	{
		gdouble fStep;
		int i;
		for (i = 0; i < iNbStripes+1; i ++)
		{
			fStep = (double)i / iNbStripes;
			cairo_pattern_add_color_stop_rgba (pStripesPattern,
				fStep - fStripesWidth / 2.,
				fStripesColorBright[0],
				fStripesColorBright[1],
				fStripesColorBright[2],
				fStripesColorBright[3]);
			cairo_pattern_add_color_stop_rgba (pStripesPattern,
				fStep,
				fStripesColorDark[0],
				fStripesColorDark[1],
				fStripesColorDark[2],
				fStripesColorDark[3]);
			cairo_pattern_add_color_stop_rgba (pStripesPattern,
				fStep + fStripesWidth / 2.,
				fStripesColorBright[0],
				fStripesColorBright[1],
				fStripesColorBright[2],
				fStripesColorBright[3]);
		}
	}
	else
	{
		cairo_pattern_add_color_stop_rgba (pStripesPattern,
			0.,
			fStripesColorDark[0],
			fStripesColorDark[1],
			fStripesColorDark[2],
			fStripesColorDark[3]);
		cairo_pattern_add_color_stop_rgba (pStripesPattern,
			1.,
			fStripesColorBright[0],
			fStripesColorBright[1],
			fStripesColorBright[2],
			fStripesColorBright[3]);
	}

	cairo_surface_t *pNewSurface = cairo_dock_create_blank_surface (
			iWidth,
			iHeight);
	cairo_t *pImageContext = cairo_create (pNewSurface);
	cairo_set_source (pImageContext, pStripesPattern);
	cairo_paint (pImageContext);

	cairo_pattern_destroy (pStripesPattern);
	cairo_destroy (pImageContext);
	
	return pNewSurface;
}
static void _cairo_dock_load_default_background (CairoDockImageBuffer *pImage, int iWidth, int iHeight)
{
	cd_debug ("%s (%s, %d, %dx%d)", __func__, myDocksParam.cBackgroundImageFile, myDocksParam.bBackgroundImageRepeat, iWidth,
				iHeight);
	if (myDocksParam.cBackgroundImageFile != NULL)
	{
		if (myDocksParam.bBackgroundImageRepeat)
		{
			cairo_surface_t *pBgSurface = cairo_dock_create_surface_from_pattern (myDocksParam.cBackgroundImageFile,
				iWidth,
				iHeight,
				myDocksParam.fBackgroundImageAlpha);
			cairo_dock_load_image_buffer_from_surface (pImage,
				pBgSurface,
				iWidth,
				iHeight);
		}
		else
		{
			cairo_dock_load_image_buffer_full (pImage,
				myDocksParam.cBackgroundImageFile,
				iWidth,
				iHeight,
				CAIRO_DOCK_FILL_SPACE,
				myDocksParam.fBackgroundImageAlpha);
		}
	}
	if (pImage->pSurface == NULL)
	{
		cairo_surface_t *pBgSurface = _cairo_dock_make_stripes_background (
			iWidth,
			iHeight,
			myDocksParam.fStripesColorBright,
			myDocksParam.fStripesColorDark,
			myDocksParam.iNbStripes,
			myDocksParam.fStripesWidth,
			myDocksParam.fStripesAngle);
		cairo_dock_load_image_buffer_from_surface (pImage,
			pBgSurface,
			iWidth,
			iHeight);
	}
}

void cairo_dock_load_dock_background (CairoDock *pDock)
{
	cairo_dock_unload_image_buffer (&pDock->backgroundBuffer);
	
	int iWidth = pDock->iDecorationsWidth;
	int iHeight = pDock->iDecorationsHeight;
	
	if (pDock->bGlobalBg || pDock->iRefCount > 0)
	{
		_cairo_dock_load_default_background (&pDock->backgroundBuffer, iWidth, iHeight);
	}
	else if (pDock->cBgImagePath != NULL)
	{
		cairo_dock_load_image_buffer (&pDock->backgroundBuffer, pDock->cBgImagePath, iWidth, iHeight, CAIRO_DOCK_FILL_SPACE);
	}
	if (pDock->backgroundBuffer.pSurface == NULL)
	{
		cairo_surface_t *pSurface = _cairo_dock_make_stripes_background (iWidth, iHeight, pDock->fBgColorBright, pDock->fBgColorDark, 0, 0., 90);
		cairo_dock_load_image_buffer_from_surface (&pDock->backgroundBuffer, pSurface, iWidth, iHeight);
	}
	gtk_widget_queue_draw (pDock->container.pWidget);
}

static gboolean _load_background_idle (CairoDock *pDock)
{
	cairo_dock_load_dock_background (pDock);
	
	pDock->iSidLoadBg = 0;
	return FALSE;
}
void cairo_dock_trigger_load_dock_background (CairoDock *pDock)
{
	if (pDock->iDecorationsWidth == pDock->backgroundBuffer.iWidth && pDock->iDecorationsHeight == pDock->backgroundBuffer.iHeight)  // mise a jour inutile.
		return;
	if (pDock->iSidLoadBg == 0)
		pDock->iSidLoadBg = g_idle_add ((GSourceFunc)_load_background_idle, pDock);
}


void cairo_dock_make_preview (CairoDock *pDock, const gchar *cPreviewPath)
{
	if (pDock && pDock->pRenderer)
	{
		// place the mouse in the middle of the dock and update the icons position
		pDock->container.iMouseX = pDock->container.iWidth/2;
		pDock->container.iMouseY = 1;
		cairo_dock_calculate_dock_icons (pDock);
		
		// dump the context into a cairo-surface
		cairo_surface_t *pSurface;
		int w = (pDock->container.bIsHorizontal ? pDock->container.iWidth : pDock->container.iHeight);  // iActiveWidth
		int h = (pDock->container.bIsHorizontal ? pDock->container.iHeight : pDock->container.iWidth);  // iActiveHeight
		GLubyte *glbuffer = NULL;
		if (g_bUseOpenGL)
		{
			if (gldi_glx_begin_draw_container_full (CAIRO_CONTAINER (pDock), FALSE))  // FALSE to keep the color buffer (motion-blur).
			{
				glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | (pDock->pRenderer->bUseStencil && g_openglConfig.bStencilBufferAvailable ? GL_STENCIL_BUFFER_BIT : 0));
				pDock->pRenderer->render_opengl (pDock);
			}
			int s = 4;  // 4 chanels of 1 byte each (rgba).
			GLubyte *buffer = (GLubyte *) g_malloc (w * h * s);
			glbuffer = (GLubyte *) g_malloc (w * h * s);

			glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid *)buffer);

			// make upside down
			int x, y;
			for (y=0; y<h; y++) {
				for (x=0; x<w*s; x++) {
					glbuffer[y * s * w + x] = buffer[(h - y - 1) * w * s + x];
				}
			}
			
			int iStride = w * s;  // nbre d'octets entre le debut de 2 lignes.
			pSurface = cairo_image_surface_create_for_data ((guchar *)glbuffer,
				CAIRO_FORMAT_ARGB32,
				w,
				h,
				iStride);
			
			g_free (buffer);
		}
		else
		{
			pSurface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
				w,
				h);
			cairo_t *pCairoContext = cairo_create (pSurface);
			pDock->pRenderer->render (pCairoContext, pDock);
			cairo_destroy (pCairoContext);
		}
		// dump the surface into a PNG
		if (!pDock->container.bIsHorizontal)
		{
			cairo_t *pCairoContext = cairo_create (pSurface);
			cairo_translate (pCairoContext, w/2, h/2);
			cairo_rotate (pCairoContext, -G_PI/2);
			cairo_translate (pCairoContext, -h/2, -w/2);
			cairo_destroy (pCairoContext);
		}
		
		cairo_surface_write_to_png (pSurface, cPreviewPath);
		cairo_surface_destroy (pSurface);
		g_free (glbuffer);
	}
}
