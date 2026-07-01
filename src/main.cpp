#include "Application/EngineApplication.h"

int main()
{
    try
    {
        // Creamos la aplicación.
        EngineApplication app;
        // Ejecutamos todo el flujo.
        app.run();
    }
    catch (const std::exception& e)
    {
        // Captura cualquier error fatal.
        // Ejemplos futuros:
        //
        // - GPU incompatible
        // - Extensión faltante
        // - Error creando Swapchain
        // - Error creando Device
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}