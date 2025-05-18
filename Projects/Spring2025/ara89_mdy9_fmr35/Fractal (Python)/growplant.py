import turtle
import random

branch_size = 50
leaf_size = 25
turtle.speed(10)
turtle.left(90)

def drawleaf(depth):
    global leaf_size
    if (depth == 0):
        turtle.color('green')
        turtle.forward(leaf_size)
    else:
        leaf_size = leaf_size*0.95
        drawbranch(depth-1)
        pushState = turtle.pos()
        angle = random.randrange(0, 50, 5)
        turtle.right(angle)
        drawleaf(depth-1)
        turtle.goto(pushState[0], pushState[1])
        angle = random.randrange(0, 50, 5)
        turtle.left(angle)
        drawleaf(depth-1)
        turtle.goto(pushState[0], pushState[1])


def drawbranch(depth):
    global branch_size
    if (depth == 0):
        turtle.forward(branch_size)
    else:
        branch_size = branch_size*0.95
        turtle.color('brown')
        drawbranch(depth-1)

def drawplant(depth):
    global branch_size, leaf_size
    drawleaf(depth)

drawplant(6)