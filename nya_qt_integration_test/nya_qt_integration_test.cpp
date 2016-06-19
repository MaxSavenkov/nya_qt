// (C) max.savenkov@gmail.com released under the MIT license (see LICENSE)
// Based on test_cube example from nya-engine (C) nyan.developer@gmail.com released under the MIT license (see LICENSE)

/*
    The purpose of this code is to provide a more-or-less complete demonstration of integration between QtQuick component
    of Qt library and a 3D game engine (in this case, Nya engine).

    Some basic pinciples:

    1. Main loop is handled by Qt. "Game" logic is processed in on_frame function, which is called from beforeRendering signal of QQuickView
    2. Textures for UI are provided by Nya resource system
    3. QML imports are NOT provided by Nya resource system, and should be available either from disk, or from additional Qt-style (rcc+qrc) resources
    4. This example does not require MOC code generator, and demonstrates how to make QML and C++ parts work without access to it
*/

#ifdef WIN32
#include "Windows.h"
#endif

#include "QtQuick/qquickview.h"
#include "QtQuick/qquickitem.h"
#include "QtQuick/qquickimageprovider.h"

#include "QtGui/qguiapplication.h"
#include "QtGui/qopenglcontext.h"

#include "QtCore/qobject.h"
#include "QtCore/qtimer.h"
#include "QtCore/qsignalmapper.h"
#include "QtCore/qdir.h"
#include "QtCore/qtime"

#include "QtQml/qqmlabstracturlinterceptor.h"
#include "QtQml/qqmlengine.h"
#include "QtQml/qqmlcontext.h"

#include "system/app.h"
#include "system/system.h"

#include "resources/resources.h"
#include "resources/file_resources_provider.h"

#include "render/render.h"
#include "render/vbo.h"
#include "render/shader.h"
#include "render/render.h"
#include "system/shaders_cache_provider.h"

#include "log/log.h"

#include <sstream>
#include <functional>

// ----------------------------------------------------------------
// 
// Qt-derived helper classes
// 
// ----------------------------------------------------------------

  // The purposes of this class is to load images, required by QML file, from Nya engine resource system
  // wtihout modifications to QML source. Qt requires you to use "image:/provider_name/image_path/image_name.png"
  // if you want to load your images through QQuickImageProvider. However, this breaks compatibility with
  // Qt Designer. So, url_interceptor modifies incoming image URL on the fly.
class url_interceptor : public QQmlAbstractUrlInterceptor
{
public:
      // Incoming image URLs are relative to QMLEngine::baseUrl(). We need them relative to the root of our 
      // resource system. So, we are going to strip a part of URL: this call sets the part of URL to be
      // stripped.
    void set_base_url( QString& url )
    {
        m_base_url = "/" + url;
    }
      
      // The main function where we change the URL
    QUrl intercept( const QUrl &path, DataType type )
    {        
        if ( type == UrlString )
        {
            QString path_string = path.toDisplayString();
            if ( path_string.endsWith( ".png" ) )
            {
                QUrl result = path;
                  // Remove base_url from incoming URL, making it relative to our resource system's root
                QString short_path = result.path().right( result.path().length() - m_base_url.length() );
                  // Tell Qt this image should be loaded via QQuickImageProvider
                result.setScheme( "image" );
                  // Tell Qt the name of our provider
                result.setHost( "nya_provider" );
                  // Replace incoming path to image with the one in our resource system
                result.setPath( short_path );
                return result;
            }
        }

        return path;
    }

private:
    QString m_base_url;
};

  // The purposes of this class is to load images, required by QML file, from Nya engine resource system
  // wtihout modifications to QML source. Qt requires you to use "image:/provider_name/image_path/image_name.png"
  // if you want to load your images through QQuickImageProvider. However, this breaks compatibility with
  // Qt Designer. So, url_interceptor modifies incoming image URL on the fly.
class nya_image_provider : public QQuickImageProvider
{
public:
    nya_image_provider() : QQuickImageProvider( Image ) {}

      // Gets image data from Nya resource system and creates QImage from it
    virtual QImage requestImage( const QString &id, QSize *size, const QSize& requestedSize )
    {
        std::string path = id.toUtf8().constData();

        nya_resources::resource_data *data = nya_resources::get_resources_provider().access( path.c_str() );
        if ( !data )
            return QImage();

        std::vector<uchar> buf;
        buf.resize( data->get_size() );
        data->read_all( &buf[0] );

        QImage result;
        result.loadFromData( &buf[0], buf.size() );
        size->setWidth( result.width() );
        size->setHeight( result.height() );

        if ( requestedSize.isEmpty() )
            return result;

        return result.scaled( requestedSize );
    }

      // Not implemented for simplicity
    virtual QPixmap requestPixmap( const QString &id, QSize *size, const QSize& requestedSize )
    {
        return QPixmap();
    }

      // Not implemented for simplicity
    virtual QQuickTextureFactory *requestTexture( const QString &id, QSize *size, const QSize &requestedSize )
    {
        return 0;
    }
};

  // This is a helper class to allow C++ to register simple lambda functions as handlers for
  // signals incoming from QML. You can use it, for example, to convert Qt signals into your
  // own messages, by mapping all "clicked()" signals from buttons to a function which emits
  // "clicked_" + button_id.
  // 
  // The resulting dispatch is two-stage: first, QSignalMapper re-routes object's signal into
  // its own "map" slot, after which, it emits "mapped" signal - which is connected to our own
  // handling function, which performs the call of registred handler by choosing it among others
  // using generating object's registred ID.
class signal_mapper_manager
{
public:
      // The type of handling function. It is passed the registred ID of object which
      // generated the signal to allow use of generalized handlers.
    using signal_handler = std::function<void( const char* mapping_id )>;

      // Maps the signal from QML object to a C++ handler
    void connect( QObject *object, const char *signal, const char *mapping_id, signal_handler handler )
    {
        auto mapper_iter = m_mappers.find( signal );
        if ( mapper_iter == m_mappers.end() )
            mapper_iter = m_mappers.insert( std::make_pair( signal, new mapper() ) ).first;

        mapper_iter->second->connect( object, signal, mapping_id, handler );
    }

private:
      // Helper class for storing Qt's SignalMapper and performing second stage of dispatching
    class mapper
    {
    public:
        mapper()
        {
            QObject::connect( &m_qt_mapper, static_cast<void( QSignalMapper::* )( const QString& )>( &QSignalMapper::mapped ), std::bind( &mapper::mapped, this, std::placeholders::_1 ) );
        }

        void connect( QObject *object, const char *signal, const char *mapping_id, signal_handler handler )
        {
            auto r = QObject::connect( object, signal, &m_qt_mapper, SLOT( map() ) );
            m_qt_mapper.setMapping( object, mapping_id );
            m_handlers[mapping_id] = handler;
        }

    private:
        void mapped( const QString& id )
        {
            auto handler_iter = m_handlers.find( id );
            if ( handler_iter == m_handlers.end() )
                return;

            handler_iter->second( id.toUtf8().constData() );
        }

    private:
        QSignalMapper m_qt_mapper;
        std::map<QString, signal_handler> m_handlers;
    };

    std::map<std::string, mapper*> m_mappers;
};

  // This is the main application class
class test_cube : public QObject
{
public:
      // Initialize all things related to Qt
    bool qt_init()
    {
          // Create main application window
        m_qt_wnd = new QQuickView();       

          // Provide URL interceptor with base URL to strip from incoming requests
        m_interceptor.set_base_url( QDir::currentPath() );

          // Initialize and set Nya resource provider - it will be used by m_image_provider during loading
          // of QML source.
        m_resource_provider.set_folder( "./" );
        nya_resources::set_resources_provider( &m_resource_provider );

          // Register our url interceptor and image provider
        m_qt_wnd->engine()->setUrlInterceptor( &m_interceptor );
        m_qt_wnd->engine()->addImageProvider( "nya_provider", &m_image_provider );

          // Specify surface format for OpenGL context that will be created
        QSurfaceFormat format;
        format.setVersion(1, 4);
        format.setProfile(QSurfaceFormat::CompatibilityProfile);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setSwapInterval( 1 );
        format.setSwapBehavior( QSurfaceFormat::SwapBehavior::SingleBuffer );

        m_qt_wnd->setFormat(format);

          // Disable clearing buffer before rendering QtQuick controls to avoid
          // losing 3D scene.
        m_qt_wnd->setClearBeforeRendering( false );
        
          // Set window size
        m_qt_wnd->resize( 1024, 768 );
        
          // Load QML source and check for errors
        m_qt_wnd->setSource( QUrl::fromLocalFile( "nya_qt_integration_test.qml" ) );
        if ( !m_qt_wnd->errors().empty() )
            return false;

          // Connect context creation signal, to allow us to initialize Nya structures at proper time
        QObject::connect( m_qt_wnd, &QQuickView::openglContextCreated, this, &test_cube::on_init, Qt::ConnectionType::DirectConnection );

          // Connect rendering signals. This will allow us to render our 3D scene and process game logic
        QObject::connect( m_qt_wnd, &QQuickView::beforeRendering, this, &test_cube::on_frame, Qt::ConnectionType::DirectConnection );
        QObject::connect( m_qt_wnd, &QQuickView::afterRendering, this, &test_cube::on_frame_end, Qt::ConnectionType::DirectConnection );

          // Provide Nya with information about window size
        on_resize( 1024, 768 );        

          // Find QML objects and connect their relevant signals to our signal mappers
        
          // "More magic" button's clicked() signal
        QObject *b1 = m_qt_wnd->rootObject()->findChild<QObject*>( "b1" );
        m_signal_mapper.connect( b1, SIGNAL( clicked() ), "b1", [this]( const char *mapping_id ) { m_speed *= 2.0f; } );        

          // "Less magic" button's clicked() signal
        QObject *b2 = m_qt_wnd->rootObject()->findChild<QObject*>( "b2" );        
        m_signal_mapper.connect( b2, SIGNAL( clicked() ), "b2", [this]( const char *mapping_id ) { m_speed /= 2.0f; } );

          // Input field's accepted() signal
        QObject *t1 = m_qt_wnd->rootObject()->findChild<QObject*>( "t1" );
        m_signal_mapper.connect( t1, SIGNAL( accepted() ), "t1", [this, t1]( const char *mapping_id ) 
        { 
            m_speed *= 2.0f; 
            m_speed = t1->property( "text" ).toString().toFloat();
        } );

          // Find and remember label which we will use to display FPS information
        m_fps_label = m_qt_wnd->rootObject()->findChild<QObject*>( "l1" );

          // Make QML window visible (it is not, by default)
        m_qt_wnd->show();

        return true;
    }

private:
      // Initializes Nya objects
	void on_init( QOpenGLContext *context )
	{
          // Make freshly created context current
          // Note, that m_qt_wnd hasn't yet received this content, and m_qt_wnd->openglContext() == 0
          // Before the end of our initialization, we MUST remove current context (see below)
        context->makeCurrent( m_qt_wnd );

	    nya_render::set_clear_color(0.2f,0.4f,0.5f,0.0f);
		nya_render::set_clear_depth(1.0f);
        nya_render::depth_test::enable(nya_render::depth_test::less);

		float vertices[] = 
		{
			-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
		    -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f,
			-0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
			-0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
			 0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
			 0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 1.0f,
			 0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0.0f,
			 0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 1.0f
		};

		unsigned short indices[] = 
		{
			0,2,1, 1,2,3, // -x
			4,5,6, 5,7,6, // +x
			0,1,5, 0,5,4, // -y
			2,6,7, 2,7,3, // +y
			0,4,6, 0,6,2, // -z
			1,3,7, 1,7,5, // +z
		};

		m_vbo.set_vertex_data(vertices,sizeof(float)*6,8);
		m_vbo.set_vertices(0,3);
		m_vbo.set_colors(sizeof(float)*3,3);
        m_vbo.set_index_data(indices,nya_render::vbo::index2b,
			     sizeof(indices)/sizeof(unsigned short));

		const char *vs_code=
			"varying vec4 color;"
			"void main()"
			"{"
				"color=gl_Color;"
                "gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;"
			"}";

		const char *ps_code=
			"varying vec4 color;"
			"void main()"
			"{"
				"gl_FragColor=color;"
			"}";

		m_shader.add_program(nya_render::shader::vertex,vs_code);
		m_shader.add_program(nya_render::shader::pixel,ps_code);

          // Unless we remove current OpenGL context, Qt will be unable to proceed
          // with its own initialization
        context->makeCurrent( 0 );
	}

      // This is our main frame logic. Here, we render the scene,
      // and update "game logic" (rotate the cube). This function
      // is connected to beforeRenderign signal, and therefore Qt's
      // QML UI will be drawn over our images
	void on_frame()
	{
          // Calculate time since last update
        QTime currentTime = QTime::currentTime();
        int dt = m_last_update.msecsTo( currentTime );
        m_last_update= currentTime;

          // Make Qt's OpenGL context current (because it is not, at this point)
        m_qt_wnd->openglContext()->makeCurrent( m_qt_wnd );
          
          // Restore our engine's OpenGL state
        nya_render::apply_state( true );

        //nya_math::mat4 proj;
        //proj.perspective(70.0f,4/3.0,0.01f,100.0f);
        //nya_render::set_projection_matrix(proj);

        nya_render::clear(true,true);

		m_rot += dt*m_speed;
        if( m_rot > 360 )
            m_rot = 0;

		nya_math::mat4 mv;
		mv.translate(0,0,-2.0f);
		mv.rotate(30.0f,1.0f,0.0f,0.0f);
		mv.rotate(m_rot,0.0f,1.0f,0.0f);
		nya_render::set_modelview_matrix(mv);

		m_shader.bind();
		m_vbo.bind();
		m_vbo.draw();
		m_vbo.unbind();
		m_shader.unbind();

         // Restore Qt's OpenGL state
        m_qt_wnd->resetOpenGLState();

          // Every second, update FPS counter and speed display in label
	    static unsigned int fps_counter=0, fps_update_timer=0;
	    ++fps_counter;
	    fps_update_timer+=dt;
	    if( fps_update_timer > 1000 )
	    {
            std::ostringstream os;
            os << fps_counter << " fps: " << fps_counter << ", m_speed=" << m_speed;
            std::string str=os.str();
            //m_qt_wnd->setTitle( str.c_str() );
            QVariant a1 = str.c_str();
            m_fps_label->metaObject()->invokeMethod( m_fps_label, "setText", Q_ARG(QVariant, a1) );

            fps_update_timer %= 1000;
            fps_counter=0;
	    }
	}

      // This function is connected to afterRendering signal. We have
      // opportunity to do more rendering here, but instead we just tell
      // Qt not to forget to redraw everything during the next frame.
    void on_frame_end()
    {
        m_qt_wnd->update();
    }

    void on_resize(unsigned int w,unsigned int h)
    {
        if(!w || !h)
            return;

		nya_math::mat4 proj;
		proj.perspective(70.0f,float(w)/h,0.01f,100.0f);
		nya_render::set_projection_matrix(proj);

        nya_render::set_viewport( 0, 0, 1024, 768 );
    }

	void on_free()
	{
		m_vbo.release();
		m_shader.release();
	}

private:
    nya_render::vbo m_vbo;
	nya_render::shader m_shader;
    nya_resources::file_resources_provider m_resource_provider;
	
    float m_rot = 0.0f;
    float m_speed = 0.05f;

    url_interceptor m_interceptor;
    nya_image_provider m_image_provider;
    signal_mapper_manager m_signal_mapper;

    QTime m_last_update;

    QQuickView *m_qt_wnd = 0;
    
    QObject *m_fps_label = 0;
};

int CALLBACK WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
    QGuiApplication qt_app( __argc,  __argv );

    test_cube app;

    app.qt_init();
    qt_app.exec();

    return 0;
}
