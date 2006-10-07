/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.

Based on glcompmgr code by Felix Bellaby.
******************************************************************/



#include "scene_opengl.h"

#include "utils.h"
#include "client.h"

#include <X11/extensions/shape.h>

namespace KWinInternal
{

//****************************************
// SceneOpenGL
//****************************************

GLXFBConfig SceneOpenGL::fbcdrawable;
GLXContext SceneOpenGL::context;
GLXPixmap SceneOpenGL::glxroot;

const int root_attrs[] =
    {
    GLX_DOUBLEBUFFER, False,
    GLX_DEPTH_SIZE, 16,
    GLX_RED_SIZE, 1,
    GLX_GREEN_SIZE, 1,
    GLX_BLUE_SIZE, 1,
    GLX_ALPHA_SIZE, 1,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT | GLX_WINDOW_BIT,
    None
    };
    
const int drawable_attrs[] = 
    {
    GLX_DOUBLEBUFFER, False,
    GLX_DEPTH_SIZE, 0,
    GLX_RED_SIZE, 1,
    GLX_GREEN_SIZE, 1,
    GLX_BLUE_SIZE, 1,
    GLX_ALPHA_SIZE, 1,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT | GLX_WINDOW_BIT,
    None
    };

static void checkGLError( const char* txt )
    {
    GLenum err = glGetError();
    if( err != GL_NO_ERROR )
        kWarning() << "GL error (" << txt << "): 0x" << QString::number( err, 16 ) << endl;
    }

SceneOpenGL::SceneOpenGL( Workspace* ws )
    : Scene( ws )
    {
    // TODO add checks where needed
    int dummy;
    if( !glXQueryExtension( display(), &dummy, &dummy ))
        return;
    XGCValues gcattr;
    gcattr.subwindow_mode = IncludeInferiors;
    gcroot = XCreateGC( display(), rootWindow(), GCSubwindowMode, &gcattr );
    buffer = XCreatePixmap( display(), rootWindow(), displayWidth(), displayHeight(),
        QX11Info::appDepth());
    GLXFBConfig* fbconfigs = glXChooseFBConfig( display(), DefaultScreen( display()),
        root_attrs, &dummy );
    fbcroot = fbconfigs[ 0 ];
    XFree( fbconfigs );
    fbconfigs = glXChooseFBConfig( display(), DefaultScreen( display()),
        drawable_attrs, &dummy );
    fbcdrawable = fbconfigs[ 0 ];
    XFree( fbconfigs );
    glxroot = glXCreatePixmap( display(), fbcroot, buffer, NULL );
    context = glXCreateNewContext( display(), fbcroot, GLX_RGBA_TYPE, NULL, GL_FALSE );
    glXMakeContextCurrent( display(), glxroot, glxroot, context );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho( 0, displayWidth(), 0, displayHeight(), 0, 65535 );
    glEnable( GL_DEPTH_TEST );
    checkGLError( "Init" );
    }

SceneOpenGL::~SceneOpenGL()
    {
    for( QMap< Toplevel*, Window >::Iterator it = windows.begin();
         it != windows.end();
         ++it )
        (*it).free();
    glXDestroyPixmap( display(), glxroot );
    XFreeGC( display(), gcroot );
    XFreePixmap( display(), buffer );
    glXDestroyContext( display(), context );
    checkGLError( "Cleanup" );
    }

static void quadDraw( int x, int y, int w, int h )
    {
    glTexCoord2i( x, y );
    glVertex2i( x, y );
    glTexCoord2i( x + w, y );
    glVertex2i( x + w, y );
    glTexCoord2i( x + w, y + h );
    glVertex2i( x + w, y + h );
    glTexCoord2i( x, y + h );
    glVertex2i( x, y + h );
    }

void SceneOpenGL::paint( QRegion, ToplevelList windows )
    {
    grabXServer();
    glXWaitX();
    glClearColor( 0, 0, 0, 1 );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    int depth = 0;
    QList< Window* > phase2;
    for( int i = windows.count() - 1; // top to bottom
         i >= 0;
         --i )
        {
        Toplevel* c = windows[ i ];
        assert( this->windows.contains( c ));
        Window& w = this->windows[ c ];
        w.setDepth( --depth );
        if( !w.isVisible())
            continue;
        if( !w.isOpaque())
            {
            phase2.prepend( &w );
            continue;
            }
        w.bindTexture();
        w.draw();
        }
    foreach( Window* w2, phase2 )
        {
        Window& w = *w2;
        w.bindTexture();
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        w.draw();
        glDisable( GL_BLEND );
        }
    glFlush();
    glXWaitGL();
    XCopyArea( display(), buffer, rootWindow(), gcroot, 0, 0, displayWidth(), displayHeight(), 0, 0 );
    ungrabXServer();
    XFlush( display());
    checkGLError( "PostPaint" );
    }

void SceneOpenGL::windowAdded( Toplevel* c )
    {
    assert( !windows.contains( c ));
    windows[ c ] = Window( c );
    }

void SceneOpenGL::windowDeleted( Toplevel* c )
    {
    assert( windows.contains( c ));
    windows[ c ].free();
    windows.remove( c );
    }

void SceneOpenGL::windowGeometryShapeChanged( Toplevel* c )
    {
    if( !windows.contains( c )) // this is ok, shape is not valid
        return;                 // by default
    Window& w = windows[ c ];
    w.discardShape();
    w.discardTexture();
    }

void SceneOpenGL::windowOpacityChanged( Toplevel* )
    {
#if 0 // not really needed, windows are painted on every repaint
      // and opacity is used when applying texture, not when
      // creating it
    if( !windows.contains( c )) // this is ok, texture is created
        return;                 // on demand
    Window& w = windows[ c ];
    w.discardTexture();
#endif
    }

void SceneOpenGL::updateTransformation( Toplevel* )
    {
    // TODO this is only used in effects to later update
    // screen damage - since opengl doesn't use screen
    // damage, just leave this empty
    }

SceneOpenGL::Window::Window( Toplevel* c )
    : toplevel( c )
    , texture( 0 )
    , shape_valid( false )
    , depth( 0 )
    {
    }

SceneOpenGL::Window::~Window()
    {
    }
    
void SceneOpenGL::Window::free()
    {
    discardTexture();
    }

// for relative window positioning
void SceneOpenGL::Window::setDepth( int d )
    {
    depth = d;
    }

void SceneOpenGL::Window::bindTexture()
    {
    if( texture != 0 && toplevel->damage().isEmpty())
        {
        // texture doesn't need updating, just bind it
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
        return;
        }
    Pixmap pix = toplevel->windowPixmap();
    // HACK
    // When a window uses ARGB visual and has a decoration, the decoration
    // does use ARGB visual. When converting such window to a texture
    // the alpha for the decoration part is broken for some reason (undefined?).
    // I wasn't lucky converting KWin to use ARGB visuals for decorations,
    // so instead simply set alpha in those parts to opaque.
    // Without ALPHA_CLEAR_COPY the setting is done directly in the window
    // pixmap, which seems to be ok, but let's not risk trouble right now.
    // TODO check if this isn't a performance problem and how it can be done better
    Client* c = dynamic_cast< Client* >( toplevel );
    bool alpha_clear = c != NULL && c->hasAlpha() && !c->noBorder();
#define ALPHA_CLEAR_COPY
#ifdef ALPHA_CLEAR_COPY
    if( alpha_clear )
        {
        Pixmap p2 = XCreatePixmap( display(), pix, c->width(), c->height(), 32 );
        GC gc = XCreateGC( display(), pix, 0, NULL );
        XCopyArea( display(), pix, p2, gc, 0, 0, c->width(), c->height(), 0, 0 );
        pix = p2;
        XFreeGC( display(), gc );
        }
#endif
    GLXDrawable pixmap = glXCreatePixmap( display(), fbcdrawable, pix, NULL );
    glXMakeContextCurrent( display(), pixmap, pixmap, context );
    glReadBuffer( GL_FRONT );
    glDrawBuffer( GL_FRONT );
    if( alpha_clear )
        {
        glColor4f( 0, 0, 0, 1 );
        glColorMask( 0, 0, 0, 1 );
        glBegin( GL_QUADS );
        // "c->height() - ..." is to convert to opengl coords
        glVertex2i( 0, c->height() - 0 ); // left
        glVertex2i( 0, c->height() - c->height());
        glVertex2i( c->clientPos().x(), c->height() - c->height());
        glVertex2i( c->clientPos().x(), c->height() - 0 );
        glVertex2i( 0, c->height() - 0 ); // top
        glVertex2i( 0, c->height() - c->clientPos().y());
        glVertex2i( c->width(), c->height() - c->clientPos().y());
        glVertex2i( c->width(), c->height() - 0 );
        glVertex2i( c->width(), c->height() - c->height()); // right
        glVertex2i( c->width(), c->height() - 0 );
        glVertex2i( c->clientPos().x() + c->clientSize().width(), c->height() - 0 );
        glVertex2i( c->clientPos().x() + c->clientSize().width(), c->height() - c->height());
        glVertex2i( c->width(), c->height() - c->height()); // bottom
        glVertex2i( c->width(), c->height() - ( c->clientPos().y() + c->clientSize().height()));
        glVertex2i( 0, c->height() - ( c->clientPos().y() + c->clientSize().height()));
        glVertex2i( 0, c->height() - c->height());
        glEnd();
        glColorMask( 1, 1, 1, 1 );
        glColor4f( 1, 1, 1, 1 );
        }
    if( texture == None )
        {
        glGenTextures( 1, &texture );
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
        glCopyTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0,
            toplevel->hasAlpha() ? GL_RGBA : GL_RGB,
            0, 0, toplevel->width(), toplevel->height(), 0 );
        }
    else
        {
        glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texture );
        if( !toplevel->damage().isEmpty())
            {
            foreach( QRect r, toplevel->damage().rects())
                {
                int gly = height() - r.y() - r.height(); // to opengl coords
                glCopyTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0,
                    r.x(), gly, r.x(), gly, r.width(), r.height());
                }
            }
        }
    // the pixmap is no longer needed, the texture will be updated
    // only when the window changes anyway, so no need to cache
    // the pixmap
    glXDestroyPixmap( display(), pixmap );
    toplevel->resetWindowPixmap();
#ifdef ALPHA_CLEAR_COPY
    if( alpha_clear )
        XFreePixmap( display(), pix );
#endif
    }

void SceneOpenGL::Window::discardShape()
    {
    shape_valid = false;
    }

QRegion SceneOpenGL::Window::shape() const
    {
    if( !shape_valid )
        {
        if( toplevel->shape())
            {
            int count, order;
            XRectangle* rects = XShapeGetRectangles( display(), toplevel->handle(),
                ShapeBounding, &count, &order );
            if(rects)
                {
                shape_region = QRegion();
                for( int i = 0;
                     i < count;
                     ++i )
                    shape_region += QRegion( rects[ i ].x, rects[ i ].y,
                        rects[ i ].width, rects[ i ].height );
                XFree(rects);
                }
            else
                shape_region = QRegion( 0, 0, width(), height());
            }
        else
            shape_region = QRegion( 0, 0, width(), height());
        shape_valid = true;
        }
    return shape_region;
    }

void SceneOpenGL::Window::draw()
    {
// TODO for double-buffered root            glDrawBuffer( GL_BACK );
    glXMakeContextCurrent( display(), glxroot, glxroot, context );
    glPushMatrix();
    glTranslatef( glX(), glY(), depth );
    if( toplevel->opacity() != 1.0 )
        {
        glEnable( GL_BLEND );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        glColor4f( toplevel->opacity(), toplevel->opacity(), toplevel->opacity(),
            toplevel->opacity());
        }
    glEnable( GL_TEXTURE_RECTANGLE_ARB );
    glBegin( GL_QUADS );
    foreach( QRect r, shape().rects())
        quadDraw( r.x(), height() - r.y() - r.height(), r.width(), r.height());
    glEnd();
    glPopMatrix();
    if( toplevel->opacity() != 1.0 )
        {
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
        glColor4f( 0, 0, 0, 0 );
        glDisable( GL_BLEND );
        }
    glDisable( GL_TEXTURE_RECTANGLE_ARB );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
    }

bool SceneOpenGL::Window::isVisible() const
    {
    // TODO mapping state?
    return !toplevel->geometry()
        .intersect( QRect( 0, 0, displayWidth(), displayHeight()))
        .isEmpty();
    }

bool SceneOpenGL::Window::isOpaque() const
    {
    return toplevel->opacity() == 1.0 && !toplevel->hasAlpha();
    }

} // namespace
