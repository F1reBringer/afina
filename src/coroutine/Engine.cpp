#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cstring>


namespace Afina {
namespace Coroutine {

void Engine::SwitchCouroutine(Engine::context *ctx)
{
    if (cur_routine != idle_ctx)
    {
        if (setjmp(cur_routine->Environment) > 0)
        {
            return;
        }
        Store(*cur_routine);
    }
    Restore(*ctx);
}

void Engine::DeleteElement(context *&Head, context *&Element) {
    if (Element == Head)
    {
        Head = Head->next;
    }
    if (Element->prev != nullptr)
    {
        Element->prev->next = Element->next;
    }
    if (Element->next != nullptr)
    {
        Element->next->prev = Element->prev;
    }
}

void Engine::AddingToTheHead(context *&Head, context *&New_Head)
{
    if (Head == nullptr)
    {
        Head = New_Head;
        Head->next = nullptr;
    }
    else
    {
        Head->prev = New_Head;
        New_Head->next = Head;
        Head = New_Head;
    }

    Head->prev = nullptr;
}

void Engine::Store(context &ctx)
{
    char begin_address;

    if (&begin_address > ctx.Low)
    {
        ctx.Hight = &begin_address;
    }
    else
    {
        ctx.Low = &begin_address;
    }

    auto SizeOfStack = ctx.Hight - ctx.Low;  
    
    if (SizeOfStack > std::get<1>(ctx.Stack) || (SizeOfStack << 1) < std::get<1>(ctx.Stack))
    {
        delete[] std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[SizeOfStack];
        std::get<1>(ctx.Stack) = SizeOfStack;
    }

    memcpy(std::get<0>(ctx.Stack), ctx.Low, SizeOfStack);
}


void Engine::Restore(context &ctx)
{
    char begin_address;

    while (&begin_address <= ctx.Hight && &begin_address >= ctx.Low)
    {
        Restore(ctx);
    }

    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    cur_routine = &ctx;

    longjmp(ctx.Environment, 1);
}


void Engine::yield()
{
    if (!alive || (cur_routine == alive && !alive->next))
    {
    	return;
    }

    context *next_routine = alive;
    if (cur_routine == alive)
    {
        next_routine = alive->next;
    }

    SwitchCouroutine(next_routine);
}

void Engine::sched(void *routine_)
{
    auto next_routine = static_cast<context *>(routine_);
    if (next_routine == nullptr)
    {
        yield();
    }

    if (next_routine->is_blocked || next_routine == cur_routine)
    {
        return;
    }

    SwitchCouroutine(next_routine);
}


void Engine::block(void *coro)
{
    context *coro_to_block = cur_routine;

    if (coro)
    {
        coro_to_block = static_cast<context *>(coro);
    }

    if (!coro_to_block || coro_to_block->is_blocked)
    {
        return;
    }
    coro_to_block->is_blocked = true;

    DeleteElement(alive, coro_to_block);
    AddingToTheHead(blocked, coro_to_block);

    if (coro_to_block == cur_routine) {
        SwitchCouroutine(idle_ctx);
    }
}

void Engine::unblock(void *coro)
{
    auto coro_to_unblock = static_cast<context *>(coro);
    if (!coro_to_unblock || !coro_to_unblock->is_blocked)
    {
        return;
    }
    coro_to_unblock->is_blocked = false;

    DeleteElement(blocked, coro_to_unblock);
    AddingToTheHead(alive, coro_to_unblock);
}

Engine::~Engine()
{
    if (StackBottom)
    {
        delete[] std::get<0>(idle_ctx->Stack);
        delete idle_ctx;
    }

    for (auto routine = alive; routine != nullptr;)
    {
        auto tmp = routine;
        routine = routine->next;
        delete[] std::get<0>(tmp->Stack);
        delete tmp;
    }

    for (auto routine = blocked; routine != nullptr;)
    {
        auto tmp = routine;
        routine = routine->next;
        delete[] std::get<0>(tmp->Stack);
        delete tmp;
    }
}


} // namespace Coroutine
} // namespace Afina
